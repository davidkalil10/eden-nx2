// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// SPDX-License-Identifier: MPL-2.0
// Copyright © 2023 Skyline Team and Contributors (https://github.com/skyline-emu/)

package org.yuzu.yuzu_emu.features

import android.database.Cursor
import android.database.MatrixCursor
import android.os.CancellationSignal
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.DocumentsProvider
import android.webkit.MimeTypeMap
import java.io.*
import org.yuzu.yuzu_emu.BuildConfig
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.getPublicFilesDir

class DocumentProvider : DocumentsProvider() {
    private val baseDirectory: File
        get() = File(YuzuApplication.application.getPublicFilesDir().canonicalPath)

    companion object {
        private val DEFAULT_ROOT_PROJECTION: Array<String> = arrayOf(
            DocumentsContract.Root.COLUMN_ROOT_ID,
            DocumentsContract.Root.COLUMN_MIME_TYPES,
            DocumentsContract.Root.COLUMN_FLAGS,
            DocumentsContract.Root.COLUMN_ICON,
            DocumentsContract.Root.COLUMN_TITLE,
            DocumentsContract.Root.COLUMN_SUMMARY,
            DocumentsContract.Root.COLUMN_DOCUMENT_ID,
            DocumentsContract.Root.COLUMN_AVAILABLE_BYTES
        )

        private val DEFAULT_DOCUMENT_PROJECTION: Array<String> = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED,
            DocumentsContract.Document.COLUMN_FLAGS,
            DocumentsContract.Document.COLUMN_SIZE
        )

        const val AUTHORITY: String = BuildConfig.APPLICATION_ID + ".user"
        const val ROOT_ID: String = "root"

        /**
         * The file for [documentId] under [base], or null when the ID escapes it, cannot be
         * resolved (a NUL byte, a symlink loop) or names the root while [allowRoot] is false.
         */
        fun resolveDocumentFile(base: File, documentId: String, allowRoot: Boolean = true): File? {
            if (documentId != ROOT_ID && !documentId.startsWith("$ROOT_ID/")) {
                return null
            }
            val root: File
            val file: File
            try {
                root = base.canonicalFile
                file = File(root, documentId.drop(ROOT_ID.length + 1)).canonicalFile
            } catch (_: IOException) {
                return null
            }
            if (file == root) {
                return root.takeIf { allowRoot }
            }
            return file.takeIf { it.path.startsWith(root.path + File.separator) }
        }

        fun isChildDocumentId(parentDocumentId: String, documentId: String): Boolean {
            val parent = parentDocumentId.trimEnd('/')
            return documentId.startsWith("$parent/") &&
                documentId.split('/').none { it == ".." || it == "." }
        }

        /** A single path component, or null for names that would leave the directory. */
        fun sanitizeDisplayName(displayName: String): String? =
            displayName.takeIf { it.isNotEmpty() && it != "." && it != ".." && '/' !in it }

        fun conflictFreeName(name: String, index: Int): String {
            val dot = name.lastIndexOf('.')
            return if (dot <= 0) {
                "$name ($index)"
            } else {
                "${name.substring(0, dot)} ($index)${name.substring(dot)}"
            }
        }
    }

    override fun onCreate(): Boolean {
        return true
    }

    /**
     * @return The [File] that corresponds to the document ID supplied by [getDocumentId]
     */
    private fun getFile(documentId: String, allowRoot: Boolean = true): File {
        // The root is the user directory itself: renaming, moving or deleting it is refused.
        val file = resolveDocumentFile(baseDirectory, documentId, allowRoot)
            ?: throw FileNotFoundException("'$documentId' is not a document that can be used here")
        if (!file.exists()) {
            throw FileNotFoundException("${file.absolutePath} ($documentId) not found")
        }
        return file
    }

    /**
     * @return A unique ID for the provided [File]
     */
    private fun getDocumentId(file: File): String {
        return "$ROOT_ID/${file.toRelativeString(baseDirectory)}"
    }

    override fun queryRoots(projection: Array<out String>?): Cursor {
        val cursor = MatrixCursor(projection ?: DEFAULT_ROOT_PROJECTION)

        cursor.newRow().apply {
            add(DocumentsContract.Root.COLUMN_ROOT_ID, ROOT_ID)
            add(DocumentsContract.Root.COLUMN_SUMMARY, null)
            add(
                DocumentsContract.Root.COLUMN_FLAGS,
                DocumentsContract.Root.FLAG_SUPPORTS_CREATE or
                    DocumentsContract.Root.FLAG_SUPPORTS_IS_CHILD
            )
            add(DocumentsContract.Root.COLUMN_TITLE, context!!.getString(R.string.app_name))
            add(DocumentsContract.Root.COLUMN_DOCUMENT_ID, getDocumentId(baseDirectory))
            add(DocumentsContract.Root.COLUMN_MIME_TYPES, "*/*")
            add(DocumentsContract.Root.COLUMN_AVAILABLE_BYTES, baseDirectory.freeSpace)
            add(DocumentsContract.Root.COLUMN_ICON, R.drawable.ic_yuzu)
        }

        return cursor
    }

    override fun queryDocument(documentId: String?, projection: Array<out String>?): Cursor {
        val cursor = MatrixCursor(projection ?: DEFAULT_DOCUMENT_PROJECTION)
        return includeFile(cursor, documentId, null)
    }

    override fun isChildDocument(parentDocumentId: String?, documentId: String?): Boolean {
        if (parentDocumentId == null || documentId == null) return false
        return isChildDocumentId(parentDocumentId, documentId)
    }

    /**
     * @return A new [File] with a unique name based off the supplied [name], not conflicting with any existing file
     */
    private fun File.resolveWithoutConflict(displayName: String): File {
        val name = sanitizeDisplayName(displayName)
            ?: throw FileNotFoundException("Invalid document name '$displayName'")
        var file = resolve(name)
        var noConflictId = 1
        while (file.exists()) {
            file = resolve(conflictFreeName(name, noConflictId++))
        }
        return file
    }

    override fun createDocument(
        parentDocumentId: String?,
        mimeType: String?,
        displayName: String
    ): String {
        val parentFile = getFile(parentDocumentId!!)
        val newFile = parentFile.resolveWithoutConflict(displayName)

        try {
            if (DocumentsContract.Document.MIME_TYPE_DIR == mimeType) {
                if (!newFile.mkdir()) {
                    throw IOException("Failed to create directory")
                }
            } else {
                if (!newFile.createNewFile()) {
                    throw IOException("Failed to create file")
                }
            }
        } catch (e: IOException) {
            throw FileNotFoundException("Couldn't create document '${newFile.path}': ${e.message}")
        }

        return getDocumentId(newFile)
    }

    override fun deleteDocument(documentId: String?) {
        val file = getFile(documentId!!, allowRoot = false)
        if (!file.delete()) {
            throw FileNotFoundException("Couldn't delete document with ID '$documentId'")
        }
    }

    override fun removeDocument(documentId: String, parentDocumentId: String?) {
        val parent = getFile(parentDocumentId!!)
        val file = getFile(documentId, allowRoot = false)

        if (parent == file || file.parentFile == null || file.parentFile!! == parent) {
            if (!file.delete()) {
                throw FileNotFoundException("Couldn't delete document with ID '$documentId'")
            }
        } else {
            throw FileNotFoundException("Couldn't delete document with ID '$documentId'")
        }
    }

    override fun renameDocument(documentId: String?, displayName: String?): String {
        if (displayName == null) {
            throw FileNotFoundException(
                "Couldn't rename document '$documentId' as the new name is null"
            )
        }

        val sourceFile = getFile(documentId!!, allowRoot = false)
        val sourceParentFile = sourceFile.parentFile
            ?: throw FileNotFoundException(
                "Couldn't rename document '$documentId' as it has no parent"
            )
        val destFile = sourceParentFile.resolve(
            sanitizeDisplayName(displayName)
                ?: throw FileNotFoundException("Invalid document name '$displayName'")
        )

        try {
            if (!sourceFile.renameTo(destFile)) {
                throw FileNotFoundException(
                    "Couldn't rename document from '${sourceFile.name}' to '${destFile.name}'"
                )
            }
        } catch (e: Exception) {
            throw FileNotFoundException(
                "Couldn't rename document from '${sourceFile.name}' to '${destFile.name}': " +
                    "${e.message}"
            )
        }

        return getDocumentId(destFile)
    }

    private fun copyDocument(
        sourceDocumentId: String,
        sourceParentDocumentId: String,
        targetParentDocumentId: String?
    ): String {
        if (!isChildDocument(sourceParentDocumentId, sourceDocumentId)) {
            throw FileNotFoundException(
                "Couldn't copy document '$sourceDocumentId' as its parent is not " +
                    "'$sourceParentDocumentId'"
            )
        }

        return copyDocument(sourceDocumentId, targetParentDocumentId)
    }

    override fun copyDocument(sourceDocumentId: String, targetParentDocumentId: String?): String {
        val parent = getFile(targetParentDocumentId!!)
        val oldFile = getFile(sourceDocumentId)
        val newFile = parent.resolveWithoutConflict(oldFile.name)

        try {
            if (!(
                newFile.createNewFile() && newFile.setWritable(true) &&
                    newFile.setReadable(true)
                )
            ) {
                throw IOException("Couldn't create new file")
            }

            FileInputStream(oldFile).use { inStream ->
                FileOutputStream(newFile).use { outStream ->
                    inStream.copyTo(outStream)
                }
            }
        } catch (e: IOException) {
            throw FileNotFoundException("Couldn't copy document '$sourceDocumentId': ${e.message}")
        }

        return getDocumentId(newFile)
    }

    override fun moveDocument(
        sourceDocumentId: String,
        sourceParentDocumentId: String?,
        targetParentDocumentId: String?
    ): String {
        getFile(sourceDocumentId, allowRoot = false)
        try {
            val newDocumentId = copyDocument(
                sourceDocumentId,
                sourceParentDocumentId!!,
                targetParentDocumentId
            )
            removeDocument(sourceDocumentId, sourceParentDocumentId)
            return newDocumentId
        } catch (e: FileNotFoundException) {
            throw FileNotFoundException("Couldn't move document '$sourceDocumentId'")
        }
    }

    private fun includeFile(cursor: MatrixCursor, documentId: String?, file: File?): MatrixCursor {
        val localDocumentId = documentId ?: file?.let { getDocumentId(it) }
        val localFile = file ?: getFile(documentId!!)

        var flags = 0
        if (localFile.isDirectory && localFile.canWrite()) {
            flags = DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE
        } else if (localFile.canWrite()) {
            flags = DocumentsContract.Document.FLAG_SUPPORTS_WRITE
            flags = flags or DocumentsContract.Document.FLAG_SUPPORTS_DELETE

            flags = flags or DocumentsContract.Document.FLAG_SUPPORTS_REMOVE
            flags = flags or DocumentsContract.Document.FLAG_SUPPORTS_MOVE
            flags = flags or DocumentsContract.Document.FLAG_SUPPORTS_COPY
            flags = flags or DocumentsContract.Document.FLAG_SUPPORTS_RENAME
        }

        cursor.newRow().apply {
            add(DocumentsContract.Document.COLUMN_DOCUMENT_ID, localDocumentId)
            add(
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                if (localFile == baseDirectory) {
                    context!!.getString(R.string.app_name)
                } else {
                    localFile.name
                }
            )
            add(DocumentsContract.Document.COLUMN_SIZE, localFile.length())
            add(DocumentsContract.Document.COLUMN_MIME_TYPE, getTypeForFile(localFile))
            add(DocumentsContract.Document.COLUMN_LAST_MODIFIED, localFile.lastModified())
            add(DocumentsContract.Document.COLUMN_FLAGS, flags)
            if (localFile == baseDirectory) {
                add(DocumentsContract.Root.COLUMN_ICON, R.drawable.ic_yuzu)
            }
        }

        return cursor
    }

    private fun getTypeForFile(file: File): Any {
        return if (file.isDirectory) {
            DocumentsContract.Document.MIME_TYPE_DIR
        } else {
            getTypeForName(file.name)
        }
    }

    private fun getTypeForName(name: String): Any {
        val lastDot = name.lastIndexOf('.')
        if (lastDot >= 0) {
            val extension = name.substring(lastDot + 1)
            val mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension)
            if (mime != null) {
                return mime
            }
        }
        return "application/octet-stream"
    }

    override fun queryChildDocuments(
        parentDocumentId: String?,
        projection: Array<out String>?,
        sortOrder: String?
    ): Cursor {
        var cursor = MatrixCursor(projection ?: DEFAULT_DOCUMENT_PROJECTION)

        val parent = getFile(parentDocumentId!!)
        for (file in parent.listFiles()!!)
            cursor = includeFile(cursor, null, file)

        return cursor
    }

    override fun openDocument(
        documentId: String?,
        mode: String?,
        signal: CancellationSignal?
    ): ParcelFileDescriptor {
        val file = documentId?.let { getFile(it) }
        val accessMode = ParcelFileDescriptor.parseMode(mode)
        return ParcelFileDescriptor.open(file, accessMode)
    }
}
