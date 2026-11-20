// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.input

import android.os.Build
import android.view.InputDevice
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.utils.InputHandler
import org.yuzu.yuzu_emu.utils.InputHandler.getGUID
import org.yuzu.yuzu_emu.utils.Log
import org.yuzu.yuzu_emu.utils.ParamPackage

/**
 * Modern Console / Handheld Console: each controller that connects becomes the next player,
 * with its standard mapping. Handheld Console: the device's own controls come last, so a
 * connected controller is player 1. The players live in memory only: the Manual players in
 * the config are never changed. Does nothing in Manual.
 */
object ConsoleMode {
    // Values of controller_applet_mode (Settings::AppletMode)
    const val MANUAL = 0
    const val MODERN_CONSOLE = 3
    const val HANDHELD_CONSOLE = 4

    // Controllers by InputDevice.descriptor, in the order they connected
    private val connectOrder = mutableListOf<String>()

    // The last assignment sent, to skip calls that change nothing
    private var lastApplied = ""

    fun isOn(): Boolean {
        val mode = IntSetting.CONTROLLER_APPLET.getInt()
        return mode == MODERN_CONSOLE || mode == HANDHELD_CONSOLE
    }

    private class Pad(
        val descriptor: String,
        val guid: String,
        val port: Int,
        val builtIn: Boolean,
        val name: String
    )

    // The same ports InputHandler.getDevices() gives the controllers
    private fun connectedPads(): List<Pad> {
        val pads = mutableListOf<Pad>()
        val numbers = mutableSetOf<Int>()
        var port = 0
        InputDevice.getDeviceIds().forEach { id ->
            val device = InputDevice.getDevice(id) ?: return@forEach
            if (!InputHandler.isPhysicalGameController(device)) {
                return@forEach
            }
            if (numbers.add(device.controllerNumber)) {
                // Built-in controls report themselves as not external (Android 10+)
                val builtIn = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && !device.isExternal
                pads.add(Pad(device.descriptor, device.getGUID(), port, builtIn, device.name))
            }
            port++
        }
        return pads
    }

    /**
     * Assigns the players from the connected controllers. Call after any controller change;
     * force after anything that reloaded the Manual players (the boot, closing Settings).
     */
    fun apply(force: Boolean = false) {
        if (!isOn()) {
            return
        }
        val pads = connectedPads()
        connectOrder.retainAll(pads.map { it.descriptor })
        pads.forEach { if (it.descriptor !in connectOrder) connectOrder.add(it.descriptor) }

        var ordered = pads.sortedBy { connectOrder.indexOf(it.descriptor) }
        if (IntSetting.CONTROLLER_APPLET.getInt() == HANDHELD_CONSOLE) {
            ordered = ordered.filter { !it.builtIn } + ordered.filter { it.builtIn }
        }

        // The same params the controller's input events and the Manual mapping use
        val devices = ordered.take(8).map { pad ->
            ParamPackage().apply {
                set("engine", "android")
                set("guid", pad.guid)
                set("port", pad.port)
                set("display", "${pad.name} ${pad.port}")
            }.serialize()
        }
        val onHandheld = ordered.size == 1 && ordered[0].builtIn
        val assignment = devices.joinToString("|") + "|$onHandheld"
        if (!force && assignment == lastApplied) {
            return
        }
        lastApplied = assignment
        ordered.forEach { pad ->
            Log.info(
                "[ConsoleMode] ${pad.name}: port ${pad.port}, built-in ${pad.builtIn}, " +
                    "descriptor ${pad.descriptor}"
            )
        }
        applyImpl(devices.toTypedArray(), onHandheld)
    }

    private external fun applyImpl(devices: Array<String>, onHandheld: Boolean)
}
