/**************************************************************************
**
** This file is part of Installer Framework
**
** Copyright (c) 2011-2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/
#include "createshortcutoperation.h"

#include "fileutils.h"

#include <QDebug>
#include <QDir>

#include <cerrno>

using namespace QInstaller;

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>

struct DeCoInitializer
{
    DeCoInitializer()
        : neededCoInit(CoInitialize(NULL) == S_OK)
    {
    }
    ~DeCoInitializer()
    {
        if (neededCoInit)
            CoUninitialize();
    }
    bool neededCoInit;
};
#endif

static bool createLink(const QString &fileName, const QString &linkName, QString workingDir,
    QString arguments = QString())
{
    bool success = QFile::link(fileName, linkName);
#ifdef Q_OS_WIN
    if (!success)
        return success;

    if (workingDir.isEmpty())
        workingDir = QFileInfo(fileName).absolutePath();
    workingDir = QDir::toNativeSeparators(workingDir);

    // CoInitialize cleanup object
    DeCoInitializer _;

    IShellLink *psl = NULL;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl)))
        return success;

    psl->SetPath((wchar_t *)QDir::toNativeSeparators(fileName).utf16());
    psl->SetWorkingDirectory((wchar_t *)workingDir.utf16());
    if (!arguments.isNull())
        psl->SetArguments((wchar_t*)arguments.utf16());

    IPersistFile *ppf = NULL;
    if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void **)&ppf))) {
        ppf->Save((wchar_t*)QDir::toNativeSeparators(linkName).utf16(), TRUE);
        ppf->Release();
    }
    psl->Release();
#endif
    return success;
}

/*
TRANSLATOR QInstaller::CreateShortcutOperation
*/

CreateShortcutOperation::CreateShortcutOperation()
{
    setName(QLatin1String("CreateShortcut"));
}

static bool isWorkingDirOption(const QString &s)
{
    return s.startsWith(QLatin1String("workingDirectory="));
}

static QString takeWorkingDirArgument(QStringList &args)
{
    QString workingDir;
    // if the args contain an option in the form "workingDirectory=...", find it and consume it
    QStringList::iterator wdiropt = std::find_if(args.begin(), args.end(), isWorkingDirOption);
    if (wdiropt != args.end()) {
        workingDir = wdiropt->mid(QString::fromLatin1("workingDirectory=").size());
        args.erase(wdiropt);
    }
    return workingDir;
}

void CreateShortcutOperation::backup()
{
}

bool CreateShortcutOperation::performOperation()
{
    QStringList args = arguments();
    const QString workingDir = takeWorkingDirArgument(args);

    if (args.count() != 2 && args.count() != 3) {
        setError(InvalidArguments);
        setErrorString(QObject::tr("Invalid arguments: %1 arguments given, 2 or 3 expected (optional: "
            "\"workingDirectory=...\").").arg(args.count()));
        return false;
    }

    const QString& linkTarget = args.at(0);
    const QString& linkLocation = args.at(1);
    const QString targetArguments = args.value(2); //used value because it could be not existing

    const QString linkPath = QFileInfo(linkLocation).absolutePath();

    const bool linkPathAlreadyExists = QDir(linkPath).exists();
    const bool created = linkPathAlreadyExists || QDir::root().mkpath(linkPath);

    if (!created) {
        setError(UserDefinedError);
        setErrorString(tr("Could not create folder %1: %2.").arg(QDir::toNativeSeparators(linkPath),
            QLatin1String(strerror(errno))));
        return false;
    }


    //remove a possible existing older one
    QString errorString;
    if (QFile::exists(linkLocation) && !deleteFileNowOrLater(linkLocation, &errorString)) {
        setError(UserDefinedError);
        setErrorString(QObject::tr("Failed to overwrite %1: %2").arg(QDir::toNativeSeparators(linkLocation),
            errorString));
        return false;
    }

    const bool linked = createLink(linkTarget, linkLocation, workingDir, targetArguments);
    if (!linked) {
        setError(UserDefinedError);
        setErrorString(tr("Could not create link %1: %2").arg(QDir::toNativeSeparators(linkLocation),
            qt_error_string()));
        return false;
    }
    return true;
}

bool CreateShortcutOperation::undoOperation()
{
    const QStringList args = arguments();

    const QString& linkLocation = args.at(1);

    // first remove the link
    if (!deleteFileNowOrLater(linkLocation))
        qDebug() << "Can't delete:" << linkLocation;

    const QString linkPath = QFileInfo(linkLocation).absolutePath();

    QStringList pathParts = QString(linkPath).remove(QDir::homePath()).split(QLatin1String("/"));
    for (int i = pathParts.count(); i > 0; --i) {
        QString possibleToDeleteDir = QDir::homePath() + QStringList(pathParts.mid(0, i)).join(QLatin1String("/"));
        QInstaller::removeSystemGeneratedFiles(possibleToDeleteDir);
        if (!possibleToDeleteDir.isEmpty() && QDir().rmdir(possibleToDeleteDir))
            qDebug() << "Deleted directory:" << possibleToDeleteDir;
        else
            break;
    }

    return true;
}

bool CreateShortcutOperation::testOperation()
{
    return true;
}

Operation *CreateShortcutOperation::clone() const
{
    return new CreateShortcutOperation();
}
