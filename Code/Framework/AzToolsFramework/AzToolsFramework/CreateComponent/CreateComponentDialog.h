/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#if !defined(Q_MOC_RUN)
#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/UserSettings/UserSettings.h>

#include <AzCore/Component/Entity.h>

#include <AzFramework/Gem/GemInfo.h>
#include <AzToolsFramework/AssetEditor/AssetEditorBus.h>
#include <QDialog>

#endif



namespace Ui
{
    class CreateComponent;
}

namespace AZ
{
    class Component;
}

class CMakeFile
{
public:
    AZStd::string m_absolutePath;
    AZStd::string m_relativePath;

    CMakeFile(AZStd::string absolutePath, AZStd::string relativePath)
        : m_absolutePath(absolutePath)
        , m_relativePath(relativePath)
    {}
};


class CreateComponentDialog
    : public QDialog
{

    Q_OBJECT

public:

    AZ_CLASS_ALLOCATOR(CreateComponentDialog, AZ::SystemAllocator);

    explicit CreateComponentDialog(QWidget* parent = nullptr);
    ~CreateComponentDialog() override;

    void SetBaseComponent(AZStd::span<AZ::Component* const> components);

private:

    bool RunO3DE();
    void OnGemSelect(const QString& text);
    void OnNameChanged();

    QScopedPointer<Ui::CreateComponent> m_ui;

    const AZ::Component* m_baseComponent;

    AZStd::vector<AzFramework::GemInfo> m_gemInfos;
    AzFramework::GemInfo m_selectedGemInfo;

    void GetCMakeFiles();
    AZStd::vector<AZStd::string> m_cmakeFiles;
};
