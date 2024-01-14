/******************************************************************************
 *                                                                            *
 * Copyright (C) 2021 by nekohasekai <contact-sagernet@sekai.icu>             *
 *                                                                            *
 * This program is free software: you can redistribute it and/or modify       *
 * it under the terms of the GNU General Public License as published by       *
 * the Free Software Foundation, either version 3 of the License, or          *
 *  (at your option) any later version.                                       *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program. If not, see <http://www.gnu.org/licenses/>.       *
 *                                                                            *
 ******************************************************************************/

package io.nekohasekai.sagernet.plugin

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.Bundle
import android.os.ParcelFileDescriptor

abstract class NativePluginProvider : ContentProvider() {
    override fun getType(uri: Uri): String? = "application/x-elf"

    override fun onCreate(): Boolean = true

    /**
     * Provide all files needed for native plugin.
     *
     * @param provider A helper object to use to add files.
     */
    protected abstract fun populateFiles(provider: PathProvider)

    override fun query(
        uri: Uri,
        projection: Array<out String>?,
        selection: String?,
        selectionArgs: Array<out String>?,
        sortOrder: String?,
    ): Cursor? {
        check(selection == null && selectionArgs == null && sortOrder == null)
        val result = MatrixCursor(projection)
        populateFiles(PathProvider(uri, result))
        return result
    }

    /**
     * Returns executable entry absolute path.
     * This is used for fast mode initialization where ss-local launches your native binary at the path given directly.
     * In order for this to work, plugin app is encouraged to have the following in its AndroidManifest.xml:
     *  - android:installLocation="internalOnly" for <manifest>
     *  - android:extractNativeLibs="true" for <application>
     *
     * Default behavior is throwing UnsupportedOperationException. If you don't wish to use this feature, use the
     * default behavior.
     *
     * @return Absolute path for executable entry.
     */
    open fun getExecutable(): String = throw UnsupportedOperationException()

    abstract fun openFile(uri: Uri): ParcelFileDescriptor
    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
        check(mode == "r")
        return openFile(uri)
    }

    override fun call(method: String, arg: String?, extras: Bundle?): Bundle? = when (method) {
        PluginContract.METHOD_GET_EXECUTABLE -> {
            Bundle().apply {
                putString(PluginContract.EXTRA_ENTRY, getExecutable())
            }
        }
        else -> super.call(method, arg, extras)
    }

    // Methods that should not be used
    override fun insert(uri: Uri, values: ContentValues?): Uri? =
        throw UnsupportedOperationException()

    override fun update(
        uri: Uri,
        values: ContentValues?,
        selection: String?,
        selectionArgs: Array<out String>?,
    ): Int =
        throw UnsupportedOperationException()

    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int =
        throw UnsupportedOperationException()
}
