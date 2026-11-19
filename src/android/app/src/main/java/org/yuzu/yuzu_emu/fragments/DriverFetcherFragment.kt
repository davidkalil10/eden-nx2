// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.fragments

import android.os.Bundle
import androidx.fragment.app.Fragment
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.isVisible
import androidx.core.view.updatePadding
import androidx.fragment.app.activityViewModels
import androidx.recyclerview.widget.LinearLayoutManager
import com.fasterxml.jackson.databind.JsonNode
import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.transition.MaterialSharedAxis
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.DialogAddCustomDriverLinkBinding
import org.yuzu.yuzu_emu.databinding.FragmentDriverFetcherBinding
import org.yuzu.yuzu_emu.features.fetcher.CustomDriverLink
import org.yuzu.yuzu_emu.features.fetcher.CustomDriverLinkAdapter
import org.yuzu.yuzu_emu.features.fetcher.CustomDriverLinkStore
import org.yuzu.yuzu_emu.features.fetcher.isReleaseCatalogSource
import org.yuzu.yuzu_emu.features.fetcher.DriverArtifactInstaller
import org.yuzu.yuzu_emu.features.fetcher.DriverFetcherLinkParser
import org.yuzu.yuzu_emu.features.fetcher.DriverGroupAdapter
import org.yuzu.yuzu_emu.features.fetcher.DriverReleaseBackend
import org.yuzu.yuzu_emu.features.fetcher.DriverReleaseSource
import org.yuzu.yuzu_emu.features.fetcher.DriverReleasesCacheStore
import org.yuzu.yuzu_emu.features.fetcher.DriverReleasesClient
import org.yuzu.yuzu_emu.features.fetcher.DriverSourceKeys
import org.yuzu.yuzu_emu.features.fetcher.ParsedDriverLink
import org.yuzu.yuzu_emu.features.fetcher.resolveReleaseSource
import org.yuzu.yuzu_emu.model.DriverViewModel
import org.yuzu.yuzu_emu.model.HomeViewModel
import org.yuzu.yuzu_emu.utils.DirectoryInitialization
import org.yuzu.yuzu_emu.utils.GpuDriverHelper
import org.yuzu.yuzu_emu.utils.ViewUtils.updateMargins
import java.net.URL
import java.time.Instant
import java.time.LocalDateTime
import java.time.ZoneId
import kotlin.getValue

class DriverFetcherFragment : Fragment() {
    private var _binding: FragmentDriverFetcherBinding? = null
    private val binding get() = _binding!!

    private var fetchSession = 0
    private var driversJobActive = false

    enum class SortMode {
        Default, PublishTime,
    }

    private data class DriverRepo(
        val name: String = "",
        val path: String = "",
        val sort: Int = 0,
        val useTagName: Boolean = false,
        val sortMode: SortMode = SortMode.Default,
        val releaseSource: DriverReleaseSource? = null,
    )

    private val repoList: List<DriverRepo> = listOf(
        DriverRepo("Mr. Purple Turnip", "MrPurple666/purple-turnip", 0),
        DriverRepo("GameHub Adreno 8xx", "crueter/GameHub-8Elite-Drivers", 1),
        DriverRepo("KIMCHI Turnip", "K11MCH1/AdrenoToolsDrivers", 2, true, SortMode.PublishTime),
        DriverRepo("Weab-Chan Freedreno", "Weab-chan/freedreno_turnip-CI", 3),
        DriverRepo("Whitebelyash Turnip", "whitebelyash/freedreno_turnip-CI", sort=4, false, SortMode.PublishTime),
    )

    private val driverMap = listOf(
        IntRange(Integer.MIN_VALUE, 9) to "Unsupported",
        IntRange(10, 99) to "KIMCHI Latest",
        IntRange(100, 599) to "Unsupported",
        IntRange(600, 639) to "Mr. Purple EOL-24.3.4",
        IntRange(640, 699) to "Mr. Purple T19",
        IntRange(700, 710) to "Mr. Purple T29",
        IntRange(711, 899) to "Mr. Purple T30",
        IntRange(900, Int.MAX_VALUE) to "Unsupported",
    )

    private lateinit var driverGroupAdapter: DriverGroupAdapter
    private lateinit var customDriverLinkAdapter: CustomDriverLinkAdapter
    private val homeViewModel: HomeViewModel by activityViewModels()
    private val driverViewModel: DriverViewModel by activityViewModels()

    private fun recommendedDriverFor(gpuModel: String?): String {
        val adrenoModel = parseAdrenoModel(gpuModel)
        return driverMap.firstOrNull { adrenoModel in it.first }?.second ?: "Unsupported"
    }

    private fun parseAdrenoModel(gpuModel: String?): Int {
        if (gpuModel == null) {
            return 0
        }

        val modelList = gpuModel.split(" ")
        if (modelList.size < 3 || modelList[0] != "Adreno") {
            return 0
        }

        val model = modelList[2]
        return try {
            if (model.startsWith("A")) {
                model.substring(1).toInt()
            } else {
                model.toInt()
            }
        } catch (_: Exception) {
            0
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentDriverFetcherBinding.inflate(inflater)
        val gpuModel = GpuDriverHelper.tryGetGpuModel()
        binding.cardDeviceSummary.badgeRecommendedDriver.text = recommendedDriverFor(gpuModel)
        binding.cardDeviceSummary.badgeGpuModel.text = gpuModel ?: getString(R.string.unknown)

        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setStatusBarShadeVisibility(visible = false)
        binding.toolbarDrivers.setNavigationOnClickListener {
            requireActivity().onBackPressedDispatcher.onBackPressed()
        }

        binding.scrollDriverFetcher.isNestedScrollingEnabled = true
        binding.scrollDriverFetcher.isSmoothScrollingEnabled = true
        binding.listDrivers.layoutManager = LinearLayoutManager(context)
        binding.listDrivers.isNestedScrollingEnabled = false
        binding.listDrivers.setHasFixedSize(false)
        binding.listDrivers.itemAnimator = null
        driverGroupAdapter = DriverGroupAdapter(
            requireActivity(),
            driverViewModelProvider = { driverViewModel },
        )
        binding.listDrivers.adapter = driverGroupAdapter

        customDriverLinkAdapter = CustomDriverLinkAdapter(
            onDownload = { link -> downloadCustomLink(link) },
            onEdit = { link -> showCustomDriverLinkDialog(link) },
            onDelete = { link -> confirmRemoveCustomLink(link) },
        )
        binding.listCustomDriverLinks.layoutManager = LinearLayoutManager(context)
        binding.listCustomDriverLinks.adapter = customDriverLinkAdapter
        binding.listCustomDriverLinks.isNestedScrollingEnabled = false
        binding.buttonAddCustomLink.setOnClickListener { showCustomDriverLinkDialog() }
        refreshCustomDriverLinks()

        setInsets()

        fetchDrivers()
    }

    override fun onDestroyView() {
        driversJobActive = false
        fetchSession++
        super.onDestroyView()
        _binding = null
    }

    private fun refreshCustomDriverLinks() {
        val binding = _binding ?: return
        if (DirectoryInitialization.userDirectory == null) {
            customDriverLinkAdapter.submitList(emptyList())
            binding.textCustomLinksEmpty.isVisible = true
            binding.listCustomDriverLinks.isVisible = false
            return
        }
        val links = CustomDriverLinkStore.load(requireContext())
        customDriverLinkAdapter.submitList(links)
        binding.textCustomLinksEmpty.isVisible = links.isEmpty()
        binding.listCustomDriverLinks.isVisible = links.isNotEmpty()
    }

    private fun showCustomDriverLinkDialog(existing: CustomDriverLink? = null) {
        val editing = existing != null
        val dialogBinding = DialogAddCustomDriverLinkBinding.inflate(layoutInflater)
        existing?.let {
            dialogBinding.inputLabel.setText(it.label)
            dialogBinding.inputUrl.setText(it.url)
        }
        val dialog = MaterialAlertDialogBuilder(requireActivity())
            .setTitle(
                if (editing) {
                    R.string.edit_custom_driver_link
                } else {
                    R.string.add_custom_driver_link
                },
            )
            .setView(dialogBinding.root)
            .setNegativeButton(R.string.cancel, null)
            .setPositiveButton(R.string.save, null)
            .create()

        dialog.setOnShowListener {
            dialog.getButton(androidx.appcompat.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                dialogBinding.layoutLabel.error = null
                dialogBinding.layoutUrl.error = null

                val label = dialogBinding.inputLabel.text?.toString()?.trim().orEmpty()
                val url = dialogBinding.inputUrl.text?.toString()?.trim().orEmpty()

                if (label.isEmpty()) {
                    dialogBinding.layoutLabel.error =
                        getString(R.string.custom_driver_link_label_required)
                    return@setOnClickListener
                }

                val parsed = DriverFetcherLinkParser.parse(url)
                if (parsed is ParsedDriverLink.Invalid) {
                    dialogBinding.layoutUrl.error = getString(R.string.invalid_custom_driver_link_url)
                    return@setOnClickListener
                }

                val existingLinks = CustomDriverLinkStore.load(requireContext())
                val builtinKeys = DriverSourceKeys.builtinGithubKeys(repoList.map { it.path })
                if (DriverSourceKeys.isDuplicate(
                        parsed,
                        existingLinks,
                        builtinKeys,
                        exceptLinkId = existing?.id,
                    )
                ) {
                    dialogBinding.layoutUrl.error = getString(R.string.driver_source_already_added)
                    return@setOnClickListener
                }

                val wasCatalog = existing?.isReleaseCatalogSource() == true
                if (editing) {
                    CustomDriverLinkStore.updateParsed(requireContext(), existing!!.id, label, parsed)
                } else {
                    CustomDriverLinkStore.addParsed(requireContext(), label, parsed)
                }
                refreshCustomDriverLinks()
                val isCatalog = parsed is ParsedDriverLink.ReleaseRepo
                if (wasCatalog || isCatalog) {
                    reloadDriverSourceCatalog()
                }
                dialog.dismiss()
            }
        }
        dialog.show()
    }

    private fun confirmRemoveCustomLink(link: CustomDriverLink) {
        MaterialAlertDialogBuilder(requireActivity())
            .setTitle(R.string.remove_custom_driver_link)
            .setMessage(getString(R.string.remove_custom_driver_link_message, link.label))
            .setNegativeButton(R.string.cancel, null)
            .setPositiveButton(R.string.delete) { _, _ ->
                val wasCatalog = link.isReleaseCatalogSource()
                CustomDriverLinkStore.remove(requireContext(), link.id)
                refreshCustomDriverLinks()
                if (wasCatalog) {
                    reloadDriverSourceCatalog()
                }
            }
            .show()
    }

    private fun reloadDriverSourceCatalog() {
        if (_binding == null || !isAdded) {
            return
        }
        fetchDrivers()
    }

    private fun githubReleaseSource(path: String): DriverReleaseSource =
        DriverReleaseSource(
            DriverReleaseBackend.GITHUB,
            path,
            "https://api.github.com",
            "https://github.com/$path",
        )

    private fun useKimchiTagNames(path: String): Boolean = path.contains("K11MCH1")

    private fun driverGroup(
        driver: DriverRepo,
        releases: ArrayList<Release>,
    ): DriverGroup {
        val source = driver.releaseSource
            ?: error("releaseSource required")
        return DriverGroup(
            name = driver.name,
            sort = driver.sort,
            source = source,
            useTagName = driver.useTagName,
            sortMode = driver.sortMode,
            releases = releases,
        )
    }

    private fun showFetchErrorDialog(repoName: String, message: String?) {
        if (!isAdded) {
            return
        }
        MaterialAlertDialogBuilder(requireActivity())
            .setTitle(getString(R.string.error_during_fetch))
            .setMessage("${getString(R.string.failed_to_fetch)} $repoName:\n$message")
            .setPositiveButton(getString(R.string.ok)) { dialog, _ -> dialog.cancel() }
            .show()
    }

    private fun downloadCustomLink(link: CustomDriverLink) {
        if (link.resolveReleaseSource() != null) {
            return
        }
        val fileName = CustomDriverLinkStore.fileNameForLink(link)
        val normalized = DriverFetcherLinkParser.normalizeUrlInput(link.url)
        try {
            DriverArtifactInstaller.installFromUrl(
                requireActivity(),
                driverViewModel,
                URL(normalized),
                fileName,
            ) {
                refreshCustomDriverLinks()
            }
        } catch (e: Exception) {
            MaterialAlertDialogBuilder(requireActivity())
                .setTitle(R.string.driver_failed_title)
                .setMessage(e.message)
                .setPositiveButton(R.string.ok, null)
                .show()
        }
    }

    private fun fetchDrivers() {
        val binding = _binding ?: return
        binding.loadingIndicator.isVisible = true
        if (::driverGroupAdapter.isInitialized) {
            driverGroupAdapter.updateDriverGroups(emptyList())
        }

        val customCatalog = CustomDriverLinkStore.loadReleaseCatalogLinks(requireContext())
        val customRepos = customCatalog.mapIndexedNotNull { index, link ->
            val source = link.resolveReleaseSource() ?: return@mapIndexedNotNull null
            DriverRepo(
                name = link.label,
                sort = 1000 + index,
                useTagName = source.backend == DriverReleaseBackend.GITHUB &&
                    useKimchiTagNames(source.repoPath),
                sortMode = SortMode.PublishTime,
                releaseSource = source,
            )
        }

        val builtIn = repoList.map {
            it.copy(releaseSource = githubReleaseSource(it.path))
        }

        val allRepos = builtIn + customRepos
        val session = ++fetchSession
        driversJobActive = true
        val driverGroups = arrayListOf<DriverGroup>()
        val appContext = requireContext().applicationContext

        fun mergeGroup(group: DriverGroup) {
            synchronized(driverGroups) {
                val key = DriverSourceKeys.key(group.source)
                val index = driverGroups.indexOfFirst { DriverSourceKeys.key(it.source) == key }
                if (index >= 0) {
                    driverGroups[index] = group
                } else {
                    driverGroups.add(group)
                }
                driverGroups.sortBy { it.sort }
            }
        }

        fun publishGroups() {
            val binding = _binding ?: return
            if (!driversJobActive || session != fetchSession) {
                return
            }
            driverGroupAdapter.updateDriverGroups(driverGroups.toList())
            if (driverGroups.size >= allRepos.size) {
                binding.loadingIndicator.isVisible = false
            }
        }

        allRepos.forEach { driver ->
            val source = driver.releaseSource ?: return@forEach

            viewLifecycleOwner.lifecycleScope.launch {
                if (!driversJobActive || session != fetchSession) {
                    return@launch
                }

                val cached = withContext(Dispatchers.IO) {
                    DriverReleasesCacheStore.readReleases(
                        appContext,
                        source,
                        driver.useTagName,
                        driver.sortMode,
                    )
                }
                if (!cached.isNullOrEmpty()) {
                    mergeGroup(driverGroup(driver, cached))
                    publishGroups()
                }

                var releases = arrayListOf<Release>()
                var fetchFailed = false
                withContext(Dispatchers.IO) {
                    try {
                        releases = DriverReleasesClient.fetchAllReleasesRespectingCache(
                            appContext,
                            source,
                            driver.useTagName,
                            driver.sortMode,
                        )
                    } catch (e: Exception) {
                        fetchFailed = true
                        releases = cached ?: arrayListOf()
                        withContext(Dispatchers.Main) {
                            if (isAdded && driversJobActive && session == fetchSession &&
                                releases.isEmpty()
                            ) {
                                showFetchErrorDialog(driver.name, e.message)
                            }
                        }
                    }
                }

                if (!driversJobActive || session != fetchSession || _binding == null) {
                    return@launch
                }

                val publish = !fetchFailed || releases.isNotEmpty() ||
                    driverGroups.none {
                        DriverSourceKeys.key(it.source) == DriverSourceKeys.key(source)
                    }
                if (publish) {
                    mergeGroup(driverGroup(driver, releases))
                    publishGroups()
                }
            }
        }
    }

    private fun setInsets() {
        val root = _binding?.root ?: return
        ViewCompat.setOnApplyWindowInsetsListener(root) { _: View, windowInsets: WindowInsetsCompat ->
            val binding = _binding ?: return@setOnApplyWindowInsetsListener windowInsets
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right

            binding.toolbarDrivers.updateMargins(left = leftInsets, right = rightInsets)
            binding.scrollDriverFetcher.updatePadding(
                left = leftInsets,
                right = rightInsets,
                bottom = barInsets.bottom + resources.getDimensionPixelSize(R.dimen.spacing_large),
            )

            windowInsets
        }
    }

    data class Artifact(val url: URL, val name: String)

    data class Release(
        var tagName: String = "",
        var titleName: String = "",
        var title: String = "",
        var body: String = "",
        var artifacts: List<Artifact> = ArrayList(),
        var prerelease: Boolean = false,
        var latest: Boolean = false,
        var publishTime: LocalDateTime = LocalDateTime.now()
    ) {
        companion object {
            fun fromJsonArray(
                jsonString: String,
                useTagName: Boolean,
                sortMode: SortMode
            ): ArrayList<Release> {
                val mapper = jacksonObjectMapper()

                try {
                    val rootNode = mapper.readTree(jsonString)

                    val releases = ArrayList<Release>()

                    var latestRelease: Release? = null

                    if (rootNode.isArray) {
                        rootNode.forEach { node ->
                            val release = fromJson(node, useTagName)

                            if (latestRelease == null && !release.prerelease) {
                                latestRelease = release
                                release.latest = true
                            }

                            releases.add(release)
                        }
                    }

                    when (sortMode) {
                        SortMode.PublishTime -> releases.sortByDescending {
                            it.publishTime
                        }

                        else -> {}
                    }

                    return releases
                } catch (e: Exception) {
                    e.printStackTrace()
                    return ArrayList()
                }
            }

            private fun fromJson(node: JsonNode, useTagName: Boolean): Release {
                return try {
                    val tagName = node.path("tag_name").asText("")
                    val body = node.path("body").asText("")
                    val prerelease = node.path("prerelease").asBoolean(false)
                    val titleName = node.path("name").asText(tagName)

                    val published = node.path("published_at").asText("")
                    val localTime = try {
                        if (published.isNotBlank()) {
                            Instant.parse(published)
                                .atZone(ZoneId.systemDefault())
                                .toLocalDateTime()
                        } else {
                            LocalDateTime.now()
                        }
                    } catch (_: Exception) {
                        LocalDateTime.now()
                    }

                    val title = if (useTagName) tagName else titleName

                    val artifacts = ArrayList<Artifact>()
                    val assets = node.path("assets")
                    if (assets.isArray) {
                        assets.forEach { subNode ->
                            val urlStr = subNode.path("browser_download_url").asText("")
                            val name = subNode.path("name").asText("")
                            if (urlStr.isNotBlank() && name.isNotBlank()) {
                                artifacts.add(Artifact(URL(urlStr), name))
                            }
                        }
                    }

                    Release(
                        tagName,
                        titleName,
                        title,
                        body,
                        artifacts,
                        prerelease,
                        false,
                        localTime,
                    )
                } catch (_: Exception) {
                    Release()
                }
            }
        }
    }

    data class DriverGroup(
        val name: String,
        val sort: Int,
        val source: DriverReleaseSource,
        val useTagName: Boolean = false,
        val sortMode: SortMode = SortMode.Default,
        val releases: ArrayList<Release> = arrayListOf(),
    )
}
