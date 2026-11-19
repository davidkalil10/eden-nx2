// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.fetcher

import android.view.View
import android.view.ViewGroup
import androidx.core.view.isVisible
import androidx.interpolator.view.animation.FastOutSlowInInterpolator
import androidx.recyclerview.widget.RecyclerView
import androidx.transition.AutoTransition
import androidx.transition.ChangeBounds
import androidx.transition.Transition
import androidx.transition.TransitionListenerAdapter
import androidx.transition.TransitionManager
import androidx.transition.TransitionSet

/**
 * Shared height / visibility folds for driver source groups and release rows.
 */
object FetcherFoldAnimator {

    const val DURATION_MS = 240L

    private val interpolator = FastOutSlowInInterpolator()

    fun transitionHost(anchor: View): ViewGroup {
        var current: View? = anchor
        while (current != null) {
            if (current is RecyclerView) {
                return current
            }
            current = current.parent as? View
        }
        return anchor.rootView as ViewGroup
    }

    /**
     * Collapsing a driver source row: height-only on the catalog [RecyclerView] so siblings slide
     * smoothly. Expand still uses [beginFold] on the card.
     */
    fun beginGroupFold(
        anchor: View,
        vararg exclude: View,
        onEnd: (() -> Unit)? = null,
    ) {
        val sceneRoot = transitionHost(anchor)
        if (!sceneRoot.isAttachedToWindow) {
            onEnd?.invoke()
            return
        }
        val transition = TransitionSet().apply {
            ordering = TransitionSet.ORDERING_TOGETHER
            addTransition(
                ChangeBounds().apply {
                    resizeClip = true
                },
            )
            duration = DURATION_MS
            interpolator = this@FetcherFoldAnimator.interpolator
            exclude.forEach { excludeTarget(it, true) }
            if (onEnd != null) {
                addListener(
                    object : TransitionListenerAdapter() {
                        override fun onTransitionEnd(transition: Transition) {
                            onEnd()
                        }
                    },
                )
            }
        }
        TransitionManager.beginDelayedTransition(sceneRoot, transition)
    }

    /**
     * Collapse for catalogs with many releases: freeze list layout at the current pixel height so
     * [ChangeBounds] does not re-measure hundreds of release rows mid-animation.
     */
    fun beginGroupCollapse(
        anchor: View,
        releasesList: RecyclerView,
        releaseCount: Int,
        vararg exclude: View,
        onEnd: (() -> Unit)? = null,
    ) {
        var pinned = false
        if (releaseCount >= DriverGroupAdapter.LARGE_CATALOG_THRESHOLD && releasesList.isVisible) {
            releasesList.stopScroll()
            releasesList.height.takeIf { it > 0 }?.let { px ->
                val lp = releasesList.layoutParams
                lp.height = px
                releasesList.layoutParams = lp
                releasesList.setHasFixedSize(true)
                pinned = true
            }
            releasesList.suppressLayout(true)
        }

        beginGroupFold(
            anchor,
            *exclude,
            onEnd = {
                if (pinned) {
                    releasesList.suppressLayout(false)
                    val lp = releasesList.layoutParams
                    lp.height = ViewGroup.LayoutParams.WRAP_CONTENT
                    releasesList.layoutParams = lp
                    releasesList.setHasFixedSize(false)
                }
                onEnd?.invoke()
            },
        )
    }

    fun beginFold(
        sceneRoot: ViewGroup,
        vararg exclude: View,
        onEnd: (() -> Unit)? = null,
    ) {
        if (!sceneRoot.isAttachedToWindow) {
            onEnd?.invoke()
            return
        }
        val transition = AutoTransition().apply {
            duration = DURATION_MS
            this.interpolator = this@FetcherFoldAnimator.interpolator
            exclude.forEach { excludeTarget(it, true) }
            if (onEnd != null) {
                addListener(
                    object : TransitionListenerAdapter() {
                        override fun onTransitionEnd(transition: Transition) {
                            onEnd()
                        }
                    },
                )
            }
        }
        TransitionManager.beginDelayedTransition(sceneRoot, transition)
    }

    fun pinReleaseListToMaxHeight(list: RecyclerView, maxHeightPx: Int) {
        val lp = list.layoutParams
        lp.height = maxHeightPx
        list.layoutParams = lp
        list.setHasFixedSize(true)
    }

    fun resetReleaseListHeight(list: RecyclerView) {
        val lp = list.layoutParams
        lp.height = ViewGroup.LayoutParams.WRAP_CONTENT
        list.layoutParams = lp
        list.setHasFixedSize(false)
    }

    fun animateChevron(chevron: View, expanded: Boolean) {
        if (!chevron.isAttachedToWindow) {
            chevron.rotation = if (expanded) 180f else 0f
            return
        }
        chevron.animate().cancel()
        chevron.animate()
            .rotation(if (expanded) 180f else 0f)
            .setDuration(DURATION_MS)
            .setInterpolator(interpolator)
            .start()
    }
}