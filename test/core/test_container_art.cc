/*GRB*
    Gerbera - https://gerbera.io/

    Copyright (C) 2026 Gerbera Contributors

    Gerbera is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation.

    Gerbera is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Gerbera.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../mock/config_mock.h"
#include "../mock/database_mock.h"
#include "cds/cds_container.h"
#include "cds/cds_item.h"
#include "cds/cds_resource.h"
#include "config/config_definition.h"
#include "config/config_val.h"
#include "config/result/autoscan.h"
#include "config/result/directory_tweak.h"
#include "content/import_service.h"
#include "context.h"
#include "metadata/metacontent_handler.h"
#include "upnp/clients.h"
#include "util/mime.h"
#include "util/string_converter.h"
#include "util/tools.h"

#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
class ContainerArtConfig : public ConfigMock {
public:
    fs::path directory;
    std::string priority;
    std::vector<std::string> files { "%upnpclass%-%filename%.png", "folder.jpg", "%upnpclass%.png" };

    std::string getOption(ConfigVal option) const override
    {
        if (option == ConfigVal::IMPORT_RESOURCES_CONTAINERART_PRIORITY)
            return priority;
        if (option == ConfigVal::IMPORT_RESOURCES_CONTAINERART_LOCATION)
            return directory.string();
        if (option == ConfigVal::IMPORT_FILESYSTEM_CHARSET || option == ConfigVal::IMPORT_METADATA_CHARSET)
            return "UTF-8";
        return {};
    }

    std::vector<std::string> getArrayOption(ConfigVal option) const override
    {
        return option == ConfigVal::IMPORT_RESOURCES_CONTAINERART_FILE_LIST ? files : ConfigMock::getArrayOption(option);
    }
};

class ContainerArtDatabase : public DatabaseMock {
public:
    using DatabaseMock::DatabaseMock;
    std::shared_ptr<CdsContainer> container;
    std::map<int, std::shared_ptr<CdsObject>> tracks;

    bool addContainer(int parentId, std::string path, const std::shared_ptr<CdsContainer>& object, int* id) override
    {
        container = object;
        container->setID(42);
        container->setParentID(parentId);
        container->setLocation(path, CdsEntryType::VirtualContainer);
        *id = container->getID();
        return true;
    }

    std::shared_ptr<CdsObject> loadObject(int id, const std::string& group) override
    {
        const auto track = tracks.find(id);
        if (track != tracks.end())
            return track->second;
        return container && container->getID() == id ? container : nullptr;
    }
};

class ContainerArtTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        directory = fs::temp_directory_path() / ("gerbera-container-art-" + generateRandomId());
        fs::create_directories(directory / "theme");
        fs::create_directories(directory / "album");
        config = std::make_shared<ContainerArtConfig>();
        config->directory = directory / "theme";
        definition = std::make_shared<ConfigDefinition>();
        definition->init(definition);
        database = std::make_shared<ContainerArtDatabase>(config);
        converters = std::make_shared<ConverterManager>(config);
        auto mime = std::make_shared<Mime>(config);
        context = std::make_shared<Context>(definition, config, nullptr, mime, database, nullptr, converters);
    }

    void TearDown() override { fs::remove_all(directory); }

    fs::path directory;
    std::shared_ptr<ContainerArtConfig> config;
    std::shared_ptr<ConfigDefinition> definition;
    std::shared_ptr<ContainerArtDatabase> database;
    std::shared_ptr<ConverterManager> converters;
    std::shared_ptr<Context> context;
};
}

TEST_F(ContainerArtTest, TypedDefaultsPreserveNamedCategoriesLocalCoversAndLaterInheritedCovers)
{
    for (const auto* name : { "object.container-Audio.png", "object.container.album.musicAlbum-Alben.png", "object.container.album.png", "object.container.png", "Audio.png", "titel.png" })
        std::ofstream(config->directory / name) << "artwork fixture";
    ContainerArtHandler handler(context);
    std::vector<int> newIds;
    auto root = std::make_shared<CdsContainer>("Audio");
    ASSERT_TRUE(handler.fillMetadata(root, newIds));
    EXPECT_EQ(root->getResource(ResourcePurpose::Thumbnail)->getAttribute(ResourceAttribute::RESOURCE_FILE), (config->directory / "object.container-Audio.png").string());
    auto plural = std::make_shared<CdsContainer>("Alben", "object.container.album.musicAlbum");
    ASSERT_TRUE(handler.fillMetadata(plural, newIds));
    EXPECT_EQ(plural->getResource(ResourcePurpose::Thumbnail)->getAttribute(ResourceAttribute::RESOURCE_FILE), (config->directory / "object.container.album.musicAlbum-Alben.png").string());

    auto album = std::make_shared<CdsContainer>("Audio", "object.container.album.photoAlbum");
    album->setLocation(directory / "album", CdsEntryType::Directory);
    std::ofstream(directory / "album/folder.jpg") << "local cover";
    ASSERT_TRUE(handler.fillMetadata(album, newIds));
    auto cover = album->getResource(ResourcePurpose::Thumbnail);
    ASSERT_NE(cover, nullptr);
    EXPECT_EQ(cover->getAttribute(ResourceAttribute::RESOURCE_FILE), (directory / "album/folder.jpg").string());
    EXPECT_EQ(handler.fillFallbackMetadata(album), cover);
    EXPECT_TRUE(cover->getOption(RESOURCE_OPTION_CONTAINER_ART_FALLBACK).empty());

    auto photoAlbum = std::make_shared<CdsContainer>("Audio", "object.container.album.photoAlbum");
    EXPECT_FALSE(handler.fillMetadata(photoAlbum, newIds));
    auto albumFallback = handler.fillFallbackMetadata(photoAlbum);
    ASSERT_NE(albumFallback, nullptr);
    EXPECT_EQ(albumFallback, photoAlbum->getResource(ResourcePurpose::Thumbnail));
    EXPECT_EQ(albumFallback->getAttribute(ResourceAttribute::RESOURCE_FILE), (config->directory / "object.container.album.png").string());

    auto generic = std::make_shared<CdsContainer>("Unknown folder");
    EXPECT_FALSE(handler.fillMetadata(generic, newIds));
    auto genericFallback = handler.fillFallbackMetadata(generic);
    ASSERT_NE(genericFallback, nullptr);
    EXPECT_EQ(genericFallback, generic->getResource(ResourcePurpose::Thumbnail));
    EXPECT_EQ(genericFallback->getAttribute(ResourceAttribute::RESOURCE_FILE), (config->directory / "object.container.png").string());

    ImportService importer(context, converters);
    importer.run(nullptr);
    auto nested = std::make_shared<CdsContainer>("Audio", "object.container.album.musicAlbum");
    auto reference = std::make_shared<CdsContainer>("Reference");
    reference->setID(10);
    std::vector<std::shared_ptr<CdsObject>> chain { nested };
    importer.addContainerTree(0, chain, reference, newIds);
    auto artwork = database->container->getResource(ResourcePurpose::Thumbnail);
    ASSERT_NE(artwork, nullptr);
    EXPECT_EQ(artwork->getAttribute(ResourceAttribute::RESOURCE_FILE), (config->directory / "object.container.album.png").string());
    EXPECT_EQ(artwork->getOption(RESOURCE_OPTION_CONTAINER_ART_FALLBACK), "yes");
    auto actual = std::make_shared<CdsResource>(ContentHandler::FANART, ResourcePurpose::Thumbnail);
    actual->addAttribute(ResourceAttribute::RESOURCE_FILE, "/real/embedded-cover.png");
    reference->addResource(actual);
    importer.addContainerTree(0, chain, reference, newIds);
    artwork = database->container->getResource(ResourcePurpose::Thumbnail);
    ASSERT_NE(artwork, nullptr);
    EXPECT_EQ(artwork->getAttribute(ResourceAttribute::RESOURCE_FILE), "/real/embedded-cover.png");
    EXPECT_TRUE(artwork->getOption(RESOURCE_OPTION_CONTAINER_ART_FALLBACK).empty());

    config->files = { "%filename%.png" };
    ContentPathSetup legacy(config, database, definition, ConfigVal::IMPORT_RESOURCES_CONTAINERART_FILE_LIST, ConfigVal::IMPORT_RESOURCES_CONTAINERART_DIR_LIST);
    EXPECT_TRUE(legacy.getContentPath(root, SETTING_CONTAINERART, config->directory, ContentPathMode::Fallback).front().empty());
    ContainerArtHandler legacyHandler(context);
    auto legacyContainer = std::make_shared<CdsContainer>("No configured default");
    EXPECT_EQ(legacyHandler.fillFallbackMetadata(legacyContainer), nullptr);
}

TEST_F(ContainerArtTest, EmbeddedAudioArtworkReplacesExternalCoverAndKeepsEarliestTrack)
{
    config->priority = "embedded,folder,theme";
    config->files.push_back("cover.jpg");
    const auto folderPath = directory / "album";
    std::ofstream(folderPath / "cover.jpg") << "external artwork";
    auto folder = std::make_shared<CdsContainer>(CdsEntryType::Directory);
    folder->setID(20);
    folder->setLocation(folderPath, CdsEntryType::Directory);

    auto fileCover = std::make_shared<CdsResource>(ContentHandler::CONTAINERART, ResourcePurpose::Thumbnail);
    fileCover->addAttribute(ResourceAttribute::RESOURCE_FILE, (folderPath / "cover.jpg").string());
    folder->addResource(fileCover);

    const auto makeTrack = [&](int id, int trackNumber, bool embedded) {
        auto track = std::make_shared<CdsItem>(CdsEntryType::File);
        track->setID(id);
        track->setClass(UPNP_CLASS_MUSIC_TRACK);
        track->setTrackNumber(trackNumber);
        track->setLocation(folderPath / (std::to_string(id) + ".flac"), CdsEntryType::File);
        track->addResource(std::make_shared<CdsResource>(ContentHandler::DEFAULT, ResourcePurpose::Content));
        auto art = std::make_shared<CdsResource>(embedded ? ContentHandler::ID3 : ContentHandler::FANART, ResourcePurpose::Thumbnail);
        if (!embedded)
            art->addAttribute(ResourceAttribute::RESOURCE_FILE, (folderPath / "cover.jpg").string());
        track->addResource(art);
        database->tracks.emplace(id, track);
        return track;
    };
    const auto noCover = makeTrack(101, 1, false);
    const auto earlier = makeTrack(102, 2, true);
    const auto later = makeTrack(103, 3, true);

    ContentState state({}, ImportState::Created, nullptr, std::chrono::seconds(0), nullptr);
    state.setFirstObject(later, true);
    state.setFirstObject(noCover, true);
    state.setFirstObject(earlier, true);
    EXPECT_EQ(state.getFirstObject(), earlier);

    ImportService importer(context, converters);
    auto scan = std::make_shared<AutoscanDirectory>(folderPath, AutoscanScanMode::Timed, false, false);
    importer.run(nullptr, scan, folderPath);

    const auto sourceId = [](const std::shared_ptr<CdsObject>& container) {
        const auto art = container->getResource(ResourcePurpose::Thumbnail);
        return art ? art->getAttribute(ResourceAttribute::FANART_OBJ_ID) : "";
    };
    const auto scanWith = [&](const auto& track) {
        importer.finishScan(folderPath, folder, std::chrono::seconds(1), track);
    };
    scanWith(noCover);
    EXPECT_EQ(folder->getResource(ResourcePurpose::Thumbnail)->getAttribute(ResourceAttribute::RESOURCE_FILE), (folderPath / "cover.jpg").string());
    importer.updateAudioFolderArtwork(later, folder);
    EXPECT_EQ(sourceId(folder), "103");
    scanWith(earlier);
    EXPECT_EQ(sourceId(folder), "102");
    scanWith(later);
    EXPECT_EQ(sourceId(folder), "102");

    auto album = std::make_shared<CdsContainer>("Album", UPNP_CLASS_MUSIC_ALBUM);
    album->addResource(fileCover->clone());
    const std::vector<std::shared_ptr<CdsObject>> chain { album };
    std::vector<int> newIds;
    importer.addContainerTree(0, chain, noCover, newIds);
    EXPECT_EQ(database->container->getResource(ResourcePurpose::Thumbnail)->getAttribute(ResourceAttribute::RESOURCE_FILE), (folderPath / "cover.jpg").string());

    const auto addLayoutArtwork = [&](const auto& track) {
        album->setAudioArtworkSource(track->getID());
        importer.addContainerTree(0, chain, nullptr, newIds);
    };
    addLayoutArtwork(earlier);
    EXPECT_EQ(sourceId(database->container), "102");
    addLayoutArtwork(later);
    EXPECT_EQ(sourceId(database->container), "102");

    database->tracks.erase(earlier->getID());
    scanWith(later);
    EXPECT_EQ(sourceId(folder), "103");
    addLayoutArtwork(later);
    EXPECT_EQ(sourceId(database->container), "103");
}

TEST_F(ContainerArtTest, ArtworkPriorityPicksFolderOrThemeWhenConfiguredFirst)
{
    config->files.push_back("cover.jpg");
    const auto folderPath = directory / "album";
    const auto localCover = folderPath / "cover.jpg";
    const auto themeCover = config->directory / "object.container.png";
    std::ofstream(localCover) << "external artwork";
    std::ofstream(themeCover) << "theme artwork";

    auto track = std::make_shared<CdsItem>(CdsEntryType::File);
    track->setID(101);
    track->setClass(UPNP_CLASS_MUSIC_TRACK);
    track->setLocation(folderPath / "track.flac", CdsEntryType::File);
    track->addResource(std::make_shared<CdsResource>(ContentHandler::FLAC, ResourcePurpose::Thumbnail));
    const auto scan = std::make_shared<AutoscanDirectory>(folderPath, AutoscanScanMode::Timed, false, false);

    for (const auto& [priority, path] : std::map<std::string, fs::path> {
             { "folder,embedded,theme", localCover }, { "theme,embedded,folder", themeCover } }) {
        SCOPED_TRACE(priority);
        config->priority = priority;
        ImportService importer(context, converters);
        importer.run(nullptr, scan, folderPath);
        auto folder = std::make_shared<CdsContainer>(CdsEntryType::Directory);
        folder->setID(20);
        folder->setLocation(folderPath, CdsEntryType::Directory);
        importer.finishScan(folderPath, folder, std::chrono::seconds(1), track);
        auto artwork = folder->getResource(ResourcePurpose::Thumbnail);
        ASSERT_NE(artwork, nullptr);
        EXPECT_EQ(artwork->getAttribute(ResourceAttribute::RESOURCE_FILE), path.string());
    }
}
