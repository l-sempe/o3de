#include "CreateComponentDialog.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/ComponentApplicationBus.h>

#include <AzFramework/Gem/GemInfo.h>

#include <AzCore/Settings/SettingsRegistryImpl.h>

#include <CreateComponent/ui_CreateComponentDialog.h>
#include <AzCore/Utils/Utils.h>
#include <AzFramework/Process/ProcessWatcher.h>

#include <QPushButton>
#include <QProcess>

#include <API/PythonLoader.h>

#include <AzCore/std/sort.h>

#include <AzFramework/Process/ProcessCommunicator.h>
#include <AzCore/std/string/regex.h>

#pragma optimize("", off)

CreateComponentDialog::CreateComponentDialog(QWidget* parent)
    : QDialog(parent)
    , m_ui(new Ui::CreateComponent())
{
    m_ui->setupUi(this);

    m_ui->lblBaseComponent->setStyleSheet("font-size: 16px; color: #e9e9e9;");
    m_ui->lblComponentName->setStyleSheet("font-size: 16px; color: #e9e9e9;");
    m_ui->lblTitle->setStyleSheet("font-size: 16px; color: #e9e9e9;");

    m_ui->twGeneratedFiles->setWordWrap(false);
    m_ui->twGeneratedFiles->setTextElideMode(Qt::TextElideMode::ElideNone);

    connect(m_ui->pbOK, &QPushButton::clicked, this, &CreateComponentDialog::RunO3DE);

    connect(m_ui->ptComponentName, &QPlainTextEdit::textChanged, this, &CreateComponentDialog::OnNameChanged);
    connect(m_ui->cbGems, &QComboBox::currentTextChanged, this, &CreateComponentDialog::OnGemSelect);

    AZ::SettingsRegistryInterface* settingsRegistry = AZ::SettingsRegistry::Get();

    // Get all the available gems
    if (AzFramework::GetGemsInfo(m_gemInfos, *settingsRegistry))
    {
        for (auto& gem : m_gemInfos)
        {
            m_ui->cbGems->addItem(gem.m_gemName.c_str());
        }
    }
}


void CreateComponentDialog::GetCMakeFiles()
{
    m_cmakeFiles.clear();

    AZStd::string gemPath = m_selectedGemInfo.m_absoluteSourcePaths[0].c_str();

    // Get the absolute path to the .cmake files, search in subfolders as Gems and Projects may have different folder structure
    AZStd::vector<AZ::IO::FixedMaxPath> fileFilters = {
        AZStd::string::format("%s\\*.cmake", gemPath.c_str()).c_str(),
        AZStd::string::format("%s\\Code\\*.cmake", gemPath.c_str()).c_str(),
    };

    for (auto& fileFilter : fileFilters)
    {
        AZ::IO::SystemFile::FindFiles(fileFilter.c_str(), [fileFilter, this](const char* item, bool is_file)
            {
                // Skip the '.' and '..' folders
                if ((azstricmp(".", item) == 0) || (azstricmp("..", item) == 0))
                {
                    return true;
                }

                if (is_file)
                {
                    AZStd::string filterFolder;
                    AZ::StringFunc::Path::GetFolderPath(fileFilter.c_str(), filterFolder);
                    auto cmakeFile = AZStd::string::format("%s\\%s", filterFolder.c_str(), item);
                    m_cmakeFiles.push_back(cmakeFile);
                }

                return true;
            });
    }

    // We'll sort the list of files by size, this will try to ensure we get the most general *_files.cmake file
    // first and any decorated names after (i.e *_shared_files.cmake, *_editor_files.cmake)
    auto sorter = [](
        const AZStd::string& lhs, const AZStd::string& rhs) -> bool
        {
            return lhs.size() < rhs.size();
        };
    AZStd::sort(m_cmakeFiles.begin(), m_cmakeFiles.end(), sorter);

}



void CreateComponentDialog::OnNameChanged()
{
    AZ::IO::FixedMaxPath templatePath = AZ::Utils::GetEnginePath();
    templatePath /= "Templates/CPPComponent";

    const AZ::IO::FixedMaxPath destinationPath = AZ::Utils::GetGemPath(m_selectedGemInfo.m_gemName);
    destinationPath / "Code";


    AZStd::string componentName = m_ui->ptComponentName->toPlainText().toStdString().c_str();
    AZStd::string gemName = m_selectedGemInfo.m_gemName;
    AZStd::string gemPath = m_selectedGemInfo.m_absoluteSourcePaths[0].c_str();
    AZStd::string sanitizedComponentName = componentName;

    // Open the template JSON file and use it to produce the list of files that will be created
    AZ::IO::FixedMaxPath templateFile = templatePath / "template.json";
    rapidjson::Document templateDoc;

    m_ui->twGeneratedFiles->clear();
    m_ui->twGeneratedFiles->setHorizontalHeaderItem(0, new QTableWidgetItem("Generated file"));
    m_ui->twGeneratedFiles->setHorizontalHeaderItem(1, new QTableWidgetItem("CMake file"));


    GetCMakeFiles();

    // Get the list of output files
    AZStd::vector<AZStd::string> copyFilesList;
    AZStd::vector<AZStd::string> copyFilesListRelative;

    // We will open the template.json file and get the list of files that will be created
    QFile resource(QString::fromUtf8(templateFile.c_str()));
    if (resource.exists())
    {
        resource.open(QIODevice::ReadOnly);
        templateDoc.Parse(resource.readAll().data());

        auto copyFilesIterator = templateDoc.FindMember("copyFiles");
        for (auto& copyFile : copyFilesIterator->value.GetArray())
        {
            const auto& file = copyFile.FindMember("file");
            if (file->value.GetString())
            {
                AZStd::string fileName = file->value.GetString();

                AZ::StringFunc::Replace(fileName, "${Name}", componentName.c_str());
                AZ::StringFunc::Replace(fileName, "${GemName}", gemName.c_str());

                copyFilesListRelative.push_back(fileName.c_str());


                AZStd::string path = AZStd::string::format("%s/%s", destinationPath.c_str(), fileName.c_str());

                AZ::StringFunc::Path::Normalize(path);

                copyFilesList.push_back(path);

            }
        }
    }

    AZStd::regex fileEditorRegex(R"(.*editor.*)");
    AZStd::regex cmakeFilesEditorRegex(R"(*_editor_*)");

    int row = 0;
    m_ui->twGeneratedFiles->setRowCount(copyFilesListRelative.size());
    for (auto& file : copyFilesListRelative)
    {
        int index = 0;
        QComboBox* dropdown = new QComboBox();
        for (auto& cmakeFile : m_cmakeFiles)
        {
            AZStd::string cmakeFileName;
            AZ::StringFunc::Path::GetFullFileName(cmakeFile.c_str(), cmakeFileName);


            dropdown->addItem(cmakeFileName.c_str());

            
            bool matchEditorFiles = AZStd::regex_search(file.c_str(), fileEditorRegex) && AZStd::regex_search(cmakeFileName.c_str(), cmakeFilesEditorRegex);
            dropdown->setCurrentIndex(matchEditorFiles ? index : 0);

            index++;
        }

        m_ui->twGeneratedFiles->setItem(row, 0, new QTableWidgetItem(file.c_str()));
        m_ui->twGeneratedFiles->setCellWidget(row, 1, dropdown);

        ++row;
    }
}

void CreateComponentDialog::OnGemSelect(const QString& gem)
{
    for (auto& gemInfo : m_gemInfos)
    {
        if (gemInfo.m_gemName.compare(gem.toUtf8().toStdString().c_str()) == 0)
        {
            m_selectedGemInfo = gemInfo;
            break;
        }
    }

    OnNameChanged();
}

bool CreateComponentDialog::RunO3DE()
{
    AZ::IO::FixedMaxPath templatePath = AZ::Utils::GetEnginePath();
    templatePath /= "Templates/CPPComponent";

    const AZ::IO::FixedMaxPath destinationPath = AZ::Utils::GetGemPath(m_selectedGemInfo.m_gemName);
    destinationPath / "Code";

    AZStd::string componentName = m_ui->ptComponentName->toPlainText().toStdString().c_str();
    AZStd::string baseComponentName = m_ui->cbBaseComponents->currentText().toUtf8().toStdString().c_str();
    AZStd::string gemName = m_selectedGemInfo.m_gemName;
    AZStd::string sanitizedComponentName = componentName;

    AZStd::string filename = "scripts/o3de";

    AZ::IO::FixedMaxPath executablePath = AZ::Utils::GetEnginePath();
    executablePath /= filename + ".bat";

    if (!AZ::IO::SystemFile::Exists(executablePath.c_str()))
    {
        constexpr size_t MaxMessageSize = 2048;
        AZStd::array<char, MaxMessageSize> msg;
        azsnprintf(msg.data(), msg.size(),
            "The Project Manager was not found at '%s'.\nPlease verify O3DE is installed correctly and/or built if compiled from source. ",
            executablePath.c_str());

        AZ_Error("ProjectManager", false, msg.data());
        AZ::Utils::NativeErrorMessageBox("Project Manager not found", msg.data());
        return false;
    }

    AzFramework::ProcessLauncher::ProcessLaunchInfo processLaunchInfo;

    AZStd::vector<AZStd::string> launchCmd = { executablePath.String() };

    launchCmd = AZStd::vector<AZStd::string>{
                        executablePath.String(),
                        "create-from-template",
                        "-tp", templatePath.c_str(),
                        "-dp", destinationPath.c_str(),
                        AZStd::string::format("-r ${Name} %s ${GemName} %s ${SanitizedCppName} %s ${BaseComponent} %s",
                                                        componentName.c_str(),
                                                        gemName.c_str(),
                                                        sanitizedComponentName.c_str(),
                                                        baseComponentName.c_str()
                        ).c_str(),
                        "--force"
            };

    processLaunchInfo.m_commandlineParameters = AZStd::move(launchCmd);
    processLaunchInfo.m_showWindow = false;

    AZ_Info("CreateComponentDialog", "Executing o3de with parameters: %s\n", processLaunchInfo.GetCommandLineParametersAsString().c_str());

    // Now that the template has been run, we need to update the .cmake files according
    // to the table in the configuration.

    AZStd::unordered_multimap<AZStd::string, AZStd::string> cmakeFileUpdateMap;
    int rows = m_ui->twGeneratedFiles->rowCount();
    for (int i = 0; i < rows; ++i)
    {
        auto* fileRow = m_ui->twGeneratedFiles->item(i, 0);

        QComboBox* cmakeRow = qobject_cast<QComboBox*>(m_ui->twGeneratedFiles->cellWidget(i, 1));

        AZStd::string cmakeFile = cmakeRow->currentText().toStdString().c_str();
        for (auto& f : m_cmakeFiles)
        {
            if (f.contains(cmakeFile))
            {
                cmakeFile = f;
                break;
            }
        }

        cmakeFileUpdateMap.insert({ cmakeFile.c_str(), fileRow->text().toStdString().c_str() });
    }

    AZStd::unordered_set<AZStd::string> unique_keys;

    // Iterate through the unordered_multimap and insert keys into the unordered_set
    for (const auto& element : cmakeFileUpdateMap)
    {
        unique_keys.insert(element.first);
    }

    // Append the list of generated files into its corresponding .cmake file list
    AZStd::string fileContents;
    for (auto& key : unique_keys)
    {
        AZ::IO::FixedMaxPath dest = destinationPath / key;

        auto fileStringOutcome = AZ::Utils::ReadFile<AZStd::string>(dest.c_str());
        if (!fileStringOutcome)
        {
            AZ_Error("CreateComponentDialog", false, "File not found: %s", dest.c_str());
            return false;
        }

        auto asString = fileStringOutcome.GetValue();
        size_t setPos = asString.find("set(FILES");
        if (setPos == AZStd::string::npos)
        {
            AZ_Error("CreateComponentDialog", false, "set(FILES command not found in the file: %s", dest.c_str());
            return false;
        }

        size_t endPos = asString.find(")", setPos);
        if (endPos == AZStd::string::npos)
        {
            AZ_Error("CreateComponentDialog", false, "Closing parenthesis for set(FILES not found in the file: %s", dest.c_str());
            return false;
        }

        size_t insertPos = asString.rfind('\n', endPos);
        if (insertPos == AZStd::string::npos)
        {
            AZ_Error("CreateComponentDialog", false, "Newline before closing parenthesis not found in the file: %s", dest.c_str());
            return false;
        }

        // Get the files that correspond to this key
        auto range = cmakeFileUpdateMap.equal_range(key);

        for (auto it = range.first; it != range.second; ++it)
        {
            // append into open file
            asString.insert(insertPos, AZStd::string::format("    %s\n", it->second.c_str()));
        }

        static bool write = false;
        if (write)
        {
            AZ::Utils::WriteFile(asString.c_str(), dest.c_str());
        }
    }


    static bool run = true;
    if (run)
    {
        auto processWatcher = AZStd::unique_ptr<AzFramework::ProcessWatcher>(AzFramework::ProcessWatcher::LaunchProcess(processLaunchInfo, AzFramework::ProcessCommunicationType::COMMUNICATOR_TYPE_STDINOUT));
        processWatcher->WaitForProcessToExit(10);

        AzFramework::ProcessCommunicator* processCommunicator = processWatcher->GetCommunicator();
        if (processCommunicator && processCommunicator->IsValid())
        {
            AzFramework::ProcessOutput rawOutput;
            processCommunicator->ReadIntoProcessOutput(rawOutput);

            if (rawOutput.HasError())
            {
                AZ_TracePrintf("CreateComponentDialog", "%s", rawOutput.errorResult.c_str());
            }

            if (rawOutput.HasOutput())
            {
                AZ_TracePrintf("CreateComponentDialog", "%s", rawOutput.outputResult.c_str());
            }
        }
    }

    return true;
}

void CreateComponentDialog::SetBaseComponent(AZStd::span<AZ::Component* const> components)
{
    m_baseComponent = *components.begin();

    AZ::SerializeContext* serializeContext = nullptr;
    AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationBus::Events::GetSerializeContext);
    AZ_Assert(serializeContext, "Failed to acquire serialize context.");


    int index = 0;
    serializeContext->EnumerateDerived<AZ::Component>([this, &index](const AZ::SerializeContext::ClassData* classData, const AZ::Uuid&) -> bool
        {
            m_ui->cbBaseComponents->addItem(classData->m_name);

            AZStd::string name = classData->m_name;
            AZStd::string baseComponentName = m_baseComponent->RTTI_GetTypeName();
            if (name.compare(baseComponentName) == 0)
            {
                m_ui->cbBaseComponents->setCurrentIndex(index);
            }
            ++index;

            return true; // continue enumerating
        });
}

CreateComponentDialog::~CreateComponentDialog()
{}


#include <CreateComponent/moc_CreateComponentDialog.cpp>


#pragma optimize("", on)
