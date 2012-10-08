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

#include "copydirectoryoperation.h"

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>

using namespace QInstaller;

class AutoPush
{
public:
    AutoPush(CopyDirectoryOperation *op)
        : m_op(op) {}
    ~AutoPush() { m_op->setValue(QLatin1String("files"), m_files); }

    QStringList m_files;
    CopyDirectoryOperation *m_op;
};

/*
TRANSLATOR QInstaller::CopyDirectoryOperation
*/

CopyDirectoryOperation::CopyDirectoryOperation()
{
    setName(QLatin1String("CopyDirectory"));
}

void CopyDirectoryOperation::backup()
{
}

bool CopyDirectoryOperation::performOperation()
{
    const QStringList args = arguments();
    if (args.count() < 2 || args.count() > 3) {
        setError(InvalidArguments);
        setErrorString(tr("Invalid arguments in %0: %1 arguments given, expected: <source> <target> [overwrite]").arg(name())
            .arg(args.count()));
        return false;
    }
    const QString sourcePath = args.at(0);
    const QString targetPath = args.at(1);
    bool overwrite = false;

    if (args.count() > 2) {
        const QString overwriteStr = args.at(2);
        if (overwriteStr == QLatin1String("forceOverwrite")) {
            overwrite = true;
        } else {
            setError(InvalidArguments);
            setErrorString(tr("Invalid argument in %0: Third argument needs to be forceOverwrite, if specified").arg(name()));
            return false;
        }
    }

    const QFileInfo sourceInfo(sourcePath);
    const QFileInfo targetInfo(targetPath);
    if (!sourceInfo.exists() || !sourceInfo.isDir() || !targetInfo.exists() || !targetInfo.isDir()) {
        setError(InvalidArguments);
        setErrorString(tr("Invalid arguments in %0: Directories are invalid: %1 %2").arg(name())
            .arg(sourcePath).arg(targetPath));
        return false;
    }

    const QDir sourceDir = sourceInfo.absoluteDir();
    const QDir targetDir = targetInfo.absoluteDir();

    AutoPush autoPush(this);
    QDirIterator it(sourceInfo.absoluteFilePath(), QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString itemName = it.next();
        const QFileInfo itemInfo(sourceDir.absoluteFilePath(itemName));
        const QString relativePath = sourceDir.relativeFilePath(itemName);
        if (itemInfo.isSymLink()) {
            // Check if symlink target is inside copied directory
            const QString linkTarget = itemInfo.symLinkTarget();
            if (linkTarget.startsWith(sourceDir.absolutePath())) {
                // create symlink to copied location
                const QString linkTargetRelative = sourceDir.relativeFilePath(linkTarget);
                QFile(targetDir.absoluteFilePath(linkTargetRelative))
                    .link(targetDir.absoluteFilePath(relativePath));
            } else {
                // create symlink pointing to original location
                QFile(linkTarget).link(targetDir.absoluteFilePath(relativePath));
            }
            // add file entry
            autoPush.m_files.prepend(targetDir.absoluteFilePath(relativePath));
            emit outputTextChanged(autoPush.m_files.first());
        } else if (itemInfo.isDir()) {
            if (!targetDir.mkpath(targetDir.absoluteFilePath(relativePath))) {
                setError(InvalidArguments);
                setErrorString(tr("Could not create %0").arg(targetDir.absoluteFilePath(relativePath)));
                return false;
            }
        } else {
            const QString absolutePath = targetDir.absoluteFilePath(relativePath);
            if (overwrite && QFile::exists(absolutePath) && !deleteFileNowOrLater(absolutePath)) {
                setError(UserDefinedError);
                setErrorString(tr("Failed to overwrite %1").arg(absolutePath));
                return false;
            }
            QFile file(sourceDir.absoluteFilePath(itemName));
            if (!file.copy(absolutePath)) {
                setError(UserDefinedError);
                setErrorString(tr("Could not copy %0 to %1, error was: %3").arg(sourceDir.absoluteFilePath(itemName),
                               targetDir.absoluteFilePath(relativePath),
                               file.errorString()));
                return false;
            }
            autoPush.m_files.prepend(targetDir.absoluteFilePath(relativePath));
            emit outputTextChanged(autoPush.m_files.first());
        }
    }
    return true;
}

bool CopyDirectoryOperation::undoOperation()
{
    Q_ASSERT(arguments().count() == 2);

    QDir dir;
    const QStringList files = value(QLatin1String("files")).toStringList();
    foreach (const QString &file, files) {
        if (!QFile::remove(file)) {
            setError(InvalidArguments);
            setErrorString(tr("Could not remove %0").arg(file));
            return false;
        }
        dir.rmpath(QFileInfo(file).absolutePath());
        emit outputTextChanged(file);
    }

    setValue(QLatin1String("files"), QStringList());
    return true;
}

bool CopyDirectoryOperation::testOperation()
{
    return true;
}

Operation *CopyDirectoryOperation::clone() const
{
    return new CopyDirectoryOperation();
}
