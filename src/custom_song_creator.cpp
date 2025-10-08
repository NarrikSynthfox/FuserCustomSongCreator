#define NOMINMAX
#include <Windows.h>
#include <shlwapi.h>

#include "uasset.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "crc.h"
#include <optional>
#include <algorithm>
#include <array>
#include <unordered_set>
#include <format>


#include <filesystem>
namespace fs = std::filesystem;

#include "custom_song_pak_template.h"
#include "ImageFile.h"
#include "DDSFile.h"
#include "stb_image_resize.h"
#include "moggcrypt/CCallbacks.h"
#include "moggcrypt/VorbisEncrypter.h"

#include "fuser_asset.h"

#include "bass/bass.h"
#include "configfile.h"

#define RGBCX_IMPLEMENTATION
#include "rgbcx.h"

extern ConfigFile fcsc_cfg;
extern HWND G_hwnd;
bool endsWith(const std::string& str, const std::string& suffix) {
	if (str.size() >= suffix.size()) {
		return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
	}
	else {
		return false;  // The string is shorter than the suffix
	}
}

std::string formatFloatString(std::string& input, int numDecimalPlaces) {
	// Find the position of the decimal point
	size_t decimalPos = input.find('.');

	if (decimalPos != std::string::npos) {
		// Ensure that there are at least numDecimalPlaces characters after the decimal point
		size_t requiredLength = decimalPos + 1 + numDecimalPlaces;
		if (input.length() < requiredLength) {
			// Pad the string with zeros if necessary
			input.append(requiredLength - input.length(), '0');
		}
		return input.substr(0, requiredLength);
	}

	// If there's no decimal point, return the input as is
	return input;
}

struct AudioCtx {
	HSAMPLE currentMusic = 0;
	int currentDevice = -1;
	bool init = false;
	float volume;
};
AudioCtx gAudio;
bool unsavedChanges = false;
bool closePressed = false;
bool filenameArg = false;
std::string filenameArgPath;


void initAudio() {
	if (gAudio.init) {
		BASS_Free();
		gAudio.init = false;
	}

	if (!BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, 1)) {
		printf("Failed to set config: %d\n", BASS_ErrorGetCode());
		return;
	}

	if (!BASS_Init(gAudio.currentDevice, 44100, 0, G_hwnd, NULL)) {
		printf("Failed to init: %d\n", BASS_ErrorGetCode());
		return;
	}

	gAudio.init = true;
	gAudio.currentDevice = BASS_GetDevice();
	gAudio.volume = BASS_GetConfig(BASS_CONFIG_GVOL_SAMPLE) / 10000;
}

void playOgg(const std::vector<uint8_t>& ogg) {
	if (gAudio.currentMusic != 0) {
		BASS_SampleFree(gAudio.currentMusic);
	}
	gAudio.currentMusic = BASS_SampleLoad(TRUE, ogg.data(), 0, ogg.size(), 3, 0);
	if (gAudio.currentMusic == 0) {
		printf("Error while loading: %d\n", BASS_ErrorGetCode());
		return;
	}

	HCHANNEL ch = BASS_SampleGetChannel(gAudio.currentMusic, FALSE);
	if (!BASS_ChannelPlay(ch, TRUE)) {
		printf("Error while playing: %d\n", BASS_ErrorGetCode());
		return;
	}
}

void pauseMusic() {

}

void display_playable_audio(PlayableAudio& audio, std::string addString) {
	if (audio.oggData.empty()) {
		ImGui::Text("No ogg file loaded.");
		return;
	}

	auto active = BASS_ChannelIsActive(audio.channelHandle);
	if (active != BASS_ACTIVE_PLAYING) {
		std::string buttonText = "Play Audio##" + addString;
		if (addString == "") {
			buttonText = "Play Audio";
		}
		if (ImGui::Button(buttonText.c_str())) {
			if (audio.audioHandle != 0) {
				BASS_SampleFree(audio.audioHandle);
			}
			audio.audioHandle = BASS_SampleLoad(TRUE, audio.oggData.data(), 0, audio.oggData.size(), 3, 0);
			if (audio.audioHandle == 0) {
				printf("Error while loading: %d\n", BASS_ErrorGetCode());
				return;
			}

			audio.channelHandle = BASS_SampleGetChannel(audio.audioHandle, FALSE);
			if (!BASS_ChannelPlay(audio.channelHandle, TRUE)) {
				printf("Error while playing: %d\n", BASS_ErrorGetCode());
				return;
			}
		}
	}
	else {
		std::string buttonTextStop = "Stop##" + addString;
		if (addString == "") {
			buttonTextStop = "Stop";
		}
		if (ImGui::Button(buttonTextStop.c_str())) {
			BASS_ChannelStop(audio.channelHandle);
		}
	}
}

//////////////////////////////////////

struct ImGuiErrorModalManager {
	size_t error_id = 0;

	struct Error {
		size_t id;
		std::string message;
	};
	std::vector<Error> errors;

	std::string getErrorName(size_t id) {
		std::string name = "";
		name += "Error_" + std::to_string(id);
		return name;
	}

	void pushError(std::string error) {
		Error e;
		e.id = error_id++;
		e.message = error;
		ImGui::OpenPopup(getErrorName(e.id).c_str());
		errors.emplace_back(e);
	}

	void update() {

		for (auto it = errors.begin(); it != errors.end();) {
			auto&& e = *it;
			bool remove = false;

			if (ImGui::BeginPopupModal(getErrorName(e.id).c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text(e.message.c_str());
				ImGui::Separator();

				if (ImGui::Button("OK", ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
					remove = true;
				}
				ImGui::SetItemDefaultFocus();
			}

			if (remove) {
				it = errors.erase(it);
			}
			else {
				++it;
			}
		}
	}
};
ImGuiErrorModalManager errorManager;

static void ErrorModal(const char* name, const char* msg) {
	if (ImGui::BeginPopupModal(name, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text(msg);
		ImGui::Separator();

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();

		ImGui::EndPopup();
	}
}

static void HelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

template<typename T>
static void ChooseFuserEnum(const char* label, std::string& out, bool hideLast = true) {
	auto values = T::GetValues();

	int itemsCount = values.size();
	if (hideLast)
		itemsCount--;
	auto getter = [](void* data, int idx, const char** out_str) {
		auto&& d = reinterpret_cast<std::vector<std::string>*>(data);
		*out_str = (*d)[idx].c_str();
		return true;
		};

	int currentChoice = 0;
	for (size_t i = 0; i < itemsCount; ++i) {
		if (values[i] == out) {
			currentChoice = i;
			break;
		}
	}

	if (ImGui::Combo(label, &currentChoice, getter, &values, itemsCount)) {
		out = values[currentChoice];
		unsavedChanges = true;
	}
}

template<typename T>
static void ChooseFuserEnum(const char* label, typename T::Value& out, bool hideLast = true) {
	auto values = T::GetValues();
	int itemCount = values.size();
	if (hideLast)
		itemCount--;
	auto getter = [](void* data, int idx, const char** out_str) {
		auto&& d = reinterpret_cast<std::vector<std::string>*>(data);
		*out_str = (*d)[idx].c_str();
		return true;
		};

	int currentChoice = static_cast<int>(out);
	if (ImGui::Combo(label, &currentChoice, getter, &values, itemCount)) {
		out = static_cast<typename T::Value>(currentChoice);
		unsavedChanges = true;
	}
}

static std::optional<std::string> OpenFile(LPCSTR filter) {
	CHAR szFileName[MAX_PATH];

	// open a file name
	OPENFILENAME ofn;
	ZeroMemory(&szFileName, sizeof(szFileName));
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = G_hwnd;
	ofn.lpstrFile = szFileName;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFileName);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
	if (GetOpenFileNameA(&ofn)) {
		return std::string(ofn.lpstrFile);
	}

	return std::nullopt;
}

static std::optional<std::string> SaveFile(LPCSTR filter, LPCSTR ext, const std::string& fileName) {
	CHAR szFileName[MAX_PATH];

	// open a file name
	OPENFILENAME ofn;
	ZeroMemory(&szFileName, sizeof(szFileName));
	ZeroMemory(&ofn, sizeof(ofn));
	memcpy(szFileName, fileName.data(), std::min(fileName.size(), (size_t)MAX_PATH));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = G_hwnd;
	ofn.lpstrFile = szFileName;
	ofn.nMaxFile = sizeof(szFileName);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.lpstrDefExt = ext;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_OVERWRITEPROMPT;
	if (GetSaveFileName(&ofn)) {
		return std::string(ofn.lpstrFile);
	}

	return std::nullopt;
}


struct MainContext {
	std::string saveLocation;
	bool has_art;
	ImageFile art;
	ID3D11Device* g_pd3dDevice;
	struct CurrentPak {
		PakFile pak;
		AssetRoot root;
	};
	std::unique_ptr<CurrentPak> currentPak;

};
MainContext gCtx;


void load_file(DataBuffer&& dataBuf) {
	unsavedChanges = false;
	gCtx.has_art = false;
	gCtx.currentPak.reset();
	gCtx.currentPak = std::make_unique<MainContext::CurrentPak>();
	gCtx.saveLocation.clear();

	auto&& pak = gCtx.currentPak->pak;

	dataBuf.serialize(pak);
	int i = 0;
	for (auto&& e : pak.entries) {
		if (auto data = std::get_if<PakFile::PakEntry::PakAssetData>(&e.data)) {
			auto pos = e.name.find("DLC/Songs/");
			if (pos != std::string::npos) {
				std::string shortName;
				for (size_t i = 10; i < e.name.size(); ++i) {
					if (e.name[i] == '/') {
						break;
					}

					shortName += e.name[i];
				}

				//Double check we got the name correct
				if (e.name == ("DLC/Songs/" + shortName + "/Meta_" + shortName + ".uexp")) {
					gCtx.currentPak->root.shortName = shortName;
					//break;
				}
			}
			pos = e.name.find("UI/AlbumArt");
			if (pos != std::string::npos) {
				if (e.name.compare(e.name.length() - 5, 5, ".uexp") == 0) {
					if (auto texture = std::get_if<Texture2D>(&data->data.catagoryValues[0].value)) {
						pos = e.name.find("_small");
						if (pos == std::string::npos) {
							auto dds = DDSFile();
							dds.VInitializeFromRaw(&texture->mips[0].mipData[0], texture->mips[0].mipData.size(), texture->mips[0].width, texture->mips[0].height);
							gCtx.art = ImageFile();

							uint8_t* uncompressedImageData = new uint8_t[texture->mips[0].width * texture->mips[0].height * 4];
							auto width = texture->mips[0].width;
							auto height = texture->mips[0].height;

							uint8_t* uncompressedData_ = dds.VGetUncompressedImageData();
							for (int y = 0; y < height; y++) {
								for (int x = 0; x < width; x++) {
									uncompressedImageData[(x + width * y) * 4 + 0] = uncompressedData_[(x + width * (height - 1 - y)) * 3 + 0];
									uncompressedImageData[(x + width * y) * 4 + 1] = uncompressedData_[(x + width * (height - 1 - y)) * 3 + 1];
									uncompressedImageData[(x + width * y) * 4 + 2] = uncompressedData_[(x + width * (height - 1 - y)) * 3 + 2];
									uncompressedImageData[(x + width * y) * 4 + 3] = 255;
								}
							}
							gCtx.art.FromBytes(uncompressedImageData, texture->mips[0].width * texture->mips[0].height * 4, texture->mips[0].width, texture->mips[0].height);
							gCtx.has_art = true;
						}
					}
				}
			}
			i++;

		}
	}

	if (gCtx.currentPak->root.shortName.empty()) {
		printf("FATAL ERROR! No short name detected!");
		__debugbreak();
	}


	if (gCtx.has_art) {
		SongSerializationCtx ctx;
		ctx.loading = true;
		ctx.pak = &pak;
		gCtx.currentPak->root.serialize(ctx);
	}
	else {
		SongSerializationCtx ctx;
		ctx.loading = true;
		ctx.pak = &pak;
		gCtx.currentPak->root.serialize(ctx);
		std::string shortName = gCtx.currentPak->root.shortName;
		std::string songName = gCtx.currentPak->root.songName;
		std::string artistName = gCtx.currentPak->root.artistName;
		i32 bpm = gCtx.currentPak->root.bpm;
		std::string songKey = gCtx.currentPak->root.songKey;
		FuserEnums::KeyMode::Value keyMode = gCtx.currentPak->root.keyMode;
		FuserEnums::Genre::Value genre = gCtx.currentPak->root.genre;
		i32 year = gCtx.currentPak->root.year;
		std::vector<HmxAudio::PackageFile> celFusionPackageFile;
		std::vector<std::vector<HmxAudio::PackageFile>> celMoggFiles;
		std::vector<std::string> instrumentTypes;
		std::vector<std::string> celShortName;
		for (auto cel : gCtx.currentPak->root.celData) {
			celShortName.emplace_back(cel.data.shortName);
			auto&& fusionFile = cel.data.majorAssets[0].data.fusionFile.data;
			auto&& asset = std::get<HmxAssetFile>(fusionFile.file.e->getData().data.catagoryValues[0].value);
			auto&& mogg = asset.audio.audioFiles[0];

			HmxAudio::PackageFile fusionPackageFile;
			std::vector<HmxAudio::PackageFile> moggFiles;
			std::unordered_set<std::string> fusion_mogg_files;
			instrumentTypes.push_back(cel.data.instrument);

			for (auto&& file : asset.audio.audioFiles) {
				if (file.fileType == "FusionPatchResource") {
					fusionPackageFile = file;
				}
				else if (file.fileType == "MoggSampleResource") {
					moggFiles.emplace_back(file);
				}
			}
			celFusionPackageFile.emplace_back(fusionPackageFile);
			celMoggFiles.emplace_back(moggFiles);
		}

		DataBuffer dataBuf;
		dataBuf.buffer = (u8*)custom_song_pak_template;
		dataBuf.size = sizeof(custom_song_pak_template);
		load_file(std::move(dataBuf));

		gCtx.currentPak->root.shortName = shortName;
		gCtx.currentPak->root.songName = songName;
		gCtx.currentPak->root.artistName = artistName;
		gCtx.currentPak->root.bpm = bpm;
		gCtx.currentPak->root.songKey = songKey;
		gCtx.currentPak->root.keyMode = keyMode;
		gCtx.currentPak->root.genre = genre;
		gCtx.currentPak->root.year = year;

		int idx = 0;
		for (auto& cel : gCtx.currentPak->root.celData) {
			
			cel.data.shortName = celShortName[idx];
			auto&& fusionFile = cel.data.majorAssets[0].data.fusionFile.data;
			auto&& asset = std::get<HmxAssetFile>(fusionFile.file.e->getData().data.catagoryValues[0].value);
			auto&& mogg = asset.audio.audioFiles[0];

			cel.data.instrument = instrumentTypes[idx];
			HmxAudio::PackageFile* fusionPackageFile = nullptr;
			std::vector<HmxAudio::PackageFile*> moggFiles;
			std::unordered_set<std::string> fusion_mogg_files;
			if (celMoggFiles[idx].size() == 1 && cel.data.type.value != CelType::Type::Beat) {
				asset.audio.audioFiles.erase(asset.audio.audioFiles.begin() + 1);
			}
			for (auto&& file : asset.audio.audioFiles) {
				if (file.fileType == "FusionPatchResource") {
					file.resourceHeader = celFusionPackageFile[idx].resourceHeader;
					file.fileData = celFusionPackageFile[idx].fileData;
					file.fileName = celFusionPackageFile[idx].fileName;
					fusionPackageFile = &file;
				}
				else if (file.fileType == "MoggSampleResource") {
					moggFiles.emplace_back(&file);
				}
			}

			int moggidx = 0;

			for (auto& mogg : moggFiles) {
				mogg->resourceHeader = celMoggFiles[idx][moggidx].resourceHeader;
				mogg->fileData = celMoggFiles[idx][moggidx].fileData;
				mogg->fileName = celMoggFiles[idx][moggidx].fileName;
				moggidx++;
			}
			auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
			auto& map = fusion.nodes.getNode("keymap");
			if (map.children.size() == 1) {
				map.children.emplace_back(map.children[0]);
				std::string str = hmx_fusion_parser::outputData(map);
				std::vector<std::uint8_t> vec(str.begin(), str.end());
				map = hmx_fusion_parser::parseData(vec);
				auto nodes1 = std::get<hmx_fusion_nodes*>(map.children[0].value);
				auto nodes2 = std::get<hmx_fusion_nodes*>(map.children[1].value);
				nodes1->getInt("max_note") = 71;
				nodes2->getInt("root_note") = 84;
				nodes2->getInt("min_note") = 72;
			}
			idx++;
		}

	}

}

void load_template() {
	gCtx.currentPak.reset();
	DataBuffer dataBuf;
	dataBuf.buffer = (u8*)custom_song_pak_template;
	dataBuf.size = sizeof(custom_song_pak_template);
	load_file(std::move(dataBuf));
	gCtx.currentPak.get()->root.shortName = fcsc_cfg.defaultShortName;
	int celIdx = 0;
	for (auto& cel : gCtx.currentPak.get()->root.celData) {
		auto&& fusionFile = cel.data.majorAssets[0].data.fusionFile.data;
		auto&& fusionFileRiser = cel.data.songTransitionFile.data.majorAssets[0].data.fusionFile.data;
		auto&& asset = std::get<HmxAssetFile>(fusionFile.file.e->getData().data.catagoryValues[0].value);
		auto&& assetRiser = std::get<HmxAssetFile>(fusionFileRiser.file.e->getData().data.catagoryValues[0].value);

		HmxAudio::PackageFile* fusionPackageFile = nullptr;
		HmxAudio::PackageFile* fusionPackageFileRiser = nullptr;
		for (auto&& file : asset.audio.audioFiles) {
			if (file.fileType == "FusionPatchResource") {
				fusionPackageFile = &file;
			}
		}
		for (auto&& file : assetRiser.audio.audioFiles) {
			if (file.fileType == "FusionPatchResource") {
				fusionPackageFileRiser = &file;
			}
		}
		auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
		auto&& fusionRiser = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFileRiser->resourceHeader);
		float newGain = fcsc_cfg.DG0;
		float newGainRiser = fcsc_cfg.RG0;
		if (celIdx == 1) {
			newGain = fcsc_cfg.DG1;
			newGainRiser = fcsc_cfg.RG1;
		}
		else if (celIdx == 2) {
			newGain = fcsc_cfg.DG2;
			newGainRiser = fcsc_cfg.RG2;
		}
		else if (celIdx == 3) {
			newGain = fcsc_cfg.DG3;
			newGainRiser = fcsc_cfg.RG3;
		}
		std::get<hmx_fusion_nodes*>(fusion.nodes.getNode("presets").children[0].value)->getFloat("volume")=newGain;
		std::get<hmx_fusion_nodes*>(fusionRiser.nodes.getNode("presets").children[0].value)->getFloat("volume") = newGainRiser;
		celIdx++;
	}
}

void write_sig(DataBuffer outBuf, std::string outPath) {
	PakSigFile sigFile;
	sigFile.encrypted_total_hash.resize(512);

	const u32 chunkSize = 64 * 1024;
	for (size_t start = 0; start < outBuf.size; start += chunkSize) {
		size_t end = start + chunkSize;
		if (end > outBuf.size) {
			end = outBuf.size;
		}

		sigFile.chunks.emplace_back(CRC::MemCrc32(outBuf.buffer + start, end - start));
	}

	std::vector<u8> sigOutData;
	DataBuffer sigOutBuf;
	sigOutBuf.setupVector(sigOutData);
	sigOutBuf.loading = false;
	sigFile.serialize(sigOutBuf);
	sigOutBuf.finalize();

	std::ofstream outPak(outPath, std::ios_base::binary);
	outPak.write((char*)sigOutBuf.buffer, sigOutBuf.size);
}
void save_file() {
	SongSerializationCtx ctx;
	ctx.loading = false;
	ctx.pak = &gCtx.currentPak->pak;

	gCtx.currentPak->root.serialize(ctx);

	std::vector<u8> outData;
	DataBuffer outBuf;
	outBuf.setupVector(outData);
	outBuf.loading = false;
	gCtx.currentPak->pak.serialize(outBuf);
	outBuf.finalize();

	std::string basePath = fs::path(gCtx.saveLocation).parent_path().string() + "/";
	std::ofstream outPak(basePath + gCtx.currentPak->root.shortName + "_P.pak", std::ios_base::binary);
	outPak.write((char*)outBuf.buffer, outBuf.size);
	
	write_sig(outBuf, basePath + gCtx.currentPak->root.shortName + "_P.sig");
	
	unsavedChanges = false;
}


bool Error_InvalidFileName = false;
void select_save_location() {
	auto fileName = gCtx.currentPak->root.shortName + "_P.pak";
	auto location = SaveFile("Fuser Custom Song (*.pak)\0*.pak\0", "pak", fileName);
	if (location) {
		auto path = fs::path(*location);
		auto savedFileName = path.stem().string() + path.extension().string();
		if (savedFileName != fileName) {
			Error_InvalidFileName = true;
		}
		else {
			gCtx.saveLocation = *location;
			save_file();
		}
	}
}

static int ValidateShortName(ImGuiInputTextCallbackData* data) {
	if (!isalnum(data->EventChar) && data->EventChar != '_') {
		return 1;
	}

	return 0;
}

void display_main_properties() {
	auto&& root = gCtx.currentPak->root;

	if (ImGui::InputText("Short Name", &root.shortName, ImGuiInputTextFlags_CallbackCharFilter, ValidateShortName)) {
		gCtx.saveLocation.clear(); //We clear the save location, since it needs to resolve to another file path.
		unsavedChanges = true;
	}

	ImGui::SameLine();
	HelpMarker("Short name can only contain alphanumeric characters and '_'. This name is used to uniquely identify your custom song.");

	if(ImGui::InputText("Song Name", &root.songName))
		unsavedChanges = true;
	if(ImGui::InputText("Artist Name", &root.artistName))
		unsavedChanges = true;
	if(ImGui::InputScalar("BPM", ImGuiDataType_S32, &root.bpm))
		unsavedChanges = true;
	ChooseFuserEnum<FuserEnums::Key>("Key", root.songKey);
	ChooseFuserEnum<FuserEnums::KeyMode>("Mode", root.keyMode);
	ChooseFuserEnum<FuserEnums::Genre>("Genre", root.genre, false);
	if(ImGui::InputScalar("Year", ImGuiDataType_S32, &root.year))
		unsavedChanges = true;

	if (ImGui::Checkbox("Is Stream Optimized?", &root.isStreamOptimized))
		unsavedChanges = true;
	ImGui::SameLine();
	HelpMarker("When checked, the song will show up under the 'Stream Optimized' category in the game. This means it falls under a license that would make it safe for streaming/videos. Public domain songs, original songs (in-house), and some versions of the Creative Commons license fall under this category. If you're unsure, leave this unchecked.");
} 
//#include "stb_image_write.h"

void update_texture(std::string filepath, AssetLink<IconFileAsset> icon) {
	unsavedChanges = true;
	rgbcx::init();
	auto texture = &std::get<Texture2D>(icon.data.file.e->getData().data.catagoryValues[0].value);

	for (int mip_index = 0; mip_index < texture->mips.size(); mip_index++) {
		texture->mips[mip_index].width = (texture->mips[mip_index].width + 3) & ~3;
		texture->mips[mip_index].height = (texture->mips[mip_index].height + 3) & ~3;



		uint8_t* raw_data = gCtx.art.resizeAndGetData(texture->mips[mip_index].width, texture->mips[mip_index].height);

		auto width = texture->mips[mip_index].width;
		auto height = texture->mips[mip_index].height;

		uint8_t* compressedData = new uint8_t[width * height / 2];


		for (int y = 0; y < height; y += 4) {
			for (int x = 0; x < width; x += 4) {
				uint8_t* inData = new uint8_t[64]{ 0 };
				for (int ypx = 0; ypx < 4; ypx++)
				{
					for (int xpx = 0; xpx < 4; xpx++)
					{
						for (int i = 0; i < 4; i++)
						{
							inData[(((ypx * 4) + xpx) * 4) + i] = raw_data[((((y + ypx) * width) + (x + xpx)) * 4) + i];
						}
					}
				}
				uint8_t* block = &compressedData[(y / 4 * width / 4 + x / 4) * 8];
				rgbcx::encode_bc1(10, block, inData, true, false);
				delete[] inData;
			}
		}

		texture->mips[mip_index].mipData.clear();

		auto len = width * height / 2;
		for (int i = 0; i < len; i++) {
			texture->mips[mip_index].mipData.push_back(compressedData[i]);
		}
		texture->mips[mip_index].len_1 = len;
		texture->mips[mip_index].len_2 = len;
		delete[] compressedData;
		delete[] raw_data;
	}
}

void display_album_art() {
	auto&& root = gCtx.currentPak->root;

	ImGui::Text("Album art resizes to 512x512px for small and 1080x1080px for large");
	ImGui::Text("Accepted formats: bmp,png,jpg,jpeg");
	if (ImGui::Button("Import Album Art")) {
		auto file = OpenFile("Image File\0*.bmp;*.png;*.jpg;*.jpeg\0");
		if (file) {
			gCtx.art = ImageFile();
			gCtx.art.FromFile(file.value());

			update_texture(file.value(), root.large_icon_link);
			update_texture(file.value(), root.small_icon_link);


			gCtx.has_art = true;
		}
	}
	if (gCtx.has_art) {
		gCtx.art.imgui_Display(gCtx.g_pd3dDevice);
	}

}




int curCelTab = -1;
int curCelTabOffset = 0;
int lastCelTab = -1;
int currentAudioFile = 0;
int currentKeyzone = 0;
int selectedAudioFile = 0;
int curPickup = -1;
int curChord = -1;
float pickupInput = 0;
float chordInput = 0;
int chordInputTicks = 0;
std::string moggName(std::string inName) {
	return inName.substr(3, inName.length() - 8);
}
std::string lastMoggError;
bool replaceAudioLabel = false;
void display_mogg_settings(FusionFileAsset& fusionFile, size_t idx, HmxAudio::PackageFile& mogg, std::string addString) {

	auto&& header = std::get<HmxAudio::PackageFile::MoggSampleResourceHeader>(mogg.resourceHeader);
	std::string buttonText = "Replace Audio##" + addString;
	if (addString == "") {
		buttonText = "Replace Audio";
	}
	if (fusionFile.playableMoggs.size() <= idx) {
		fusionFile.playableMoggs.resize(idx + 1);
	}

	if (ImGui::Button(buttonText.c_str())) {
		auto moggFile = OpenFile("Ogg file (*.ogg)\0*.ogg\0");
		if (moggFile) {
			std::string fPath = *moggFile;
			std::ifstream infile(fPath, std::ios_base::binary);
			std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
			std::vector<u8> outData;

			try {
				VorbisEncrypter ve(&infile, 0x10, cppCallbacks);
				char buf[8192];
				size_t read = 0;
				size_t offset = 0;
				do {
					outData.resize(outData.size() + sizeof(buf));
					read = ve.ReadRaw(outData.data() + offset, 1, 8192);
					offset += read;
				} while (read != 0);

				header.sample_rate = ve.sample_rate;
				header.numberOfSamples = ve.numSamples;
			}
			catch (std::exception& e) {
				lastMoggError = e.what();
			}

			if (outData.size() > 0 && outData[0] == 0x0B) {
				std::wstring fPathW(fPath.begin(), fPath.end());
				const wchar_t* fName = PathFindFileNameW(fPathW.c_str());
				const wchar_t* fExt = PathFindExtensionW(fName);
				size_t fNameLen = fExt - fName;
				std::wstring fNameW(fName, fNameLen);
				std::string audioLabel(fNameW.begin(), fNameW.end());
				auto&& asset = std::get<HmxAssetFile>(fusionFile.file.e->getData().data.catagoryValues[0].value);
				if (replaceAudioLabel) {
					for (auto&& file : asset.audio.audioFiles) {
						if (file.fileType == "FusionPatchResource") {
							auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(file.resourceHeader);
							fusion.nodes.getNode("audio_labels").getString(moggName(mogg.fileName)) = audioLabel;
						}
					}
				}
				

				mogg.fileData = std::move(outData);
				fusionFile.playableMoggs[idx].oggData = std::move(fileData);
				unsavedChanges = true;
			}
			else {
				ImGui::OpenPopup("Ogg loading error");
			}
		}
	}



	display_playable_audio(fusionFile.playableMoggs[idx], addString);

	ImGui::InputScalar("Sample Rate", ImGuiDataType_U32, &header.sample_rate);

	ErrorModal("Ogg loading error", ("Failed to load ogg file:" + lastMoggError).c_str());
}
bool editAllMidiNote = false;

void draw_visual_keymap_rect(hmx_fusion_nodes* drawZone, ImVec2& cursorScreenPos, ImVec2 winSize, int zoneIdx)
{
	static bool hasCornerSelected = false;
	static int selectedCorner = 0;

	std::vector<ImVec2> corners{ ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("min_note")-0.5f), cursorScreenPos.y + (winSize.y / 127) * (127 - (drawZone->getInt("min_velocity") - 0.5f))),
		ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("max_note")+0.5f), cursorScreenPos.y + (winSize.y / 127) * (127 - (drawZone->getInt("min_velocity") - 0.5f))),
		ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("max_note") + 0.5f), cursorScreenPos.y + (winSize.y / 127) * (127 - (drawZone->getInt("max_velocity")+0.5f))),
		ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("min_note") - 0.5f), cursorScreenPos.y + (winSize.y / 127) * (127 - (drawZone->getInt("max_velocity") + 0.5f)))};
	ImGui::GetCurrentWindow()->DrawList->AddRectFilled(
		ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("min_note") - 0.5f), cursorScreenPos.y + (winSize.y / 127) * (127 - (drawZone->getInt("min_velocity") - 0.5f))),
		ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("max_note") + 0.5f), cursorScreenPos.y + (winSize.y / 127) * (127 - (drawZone->getInt("max_velocity") + 0.5f))),
		IM_COL32(255, 127, 0, zoneIdx == currentKeyzone ? 90 : 60)
	);
	if(zoneIdx == currentKeyzone){
		ImGui::GetCurrentWindow()->DrawList->AddRectFilled(
			ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("min_note") - 0.5f), cursorScreenPos.y + (winSize.y / 127) * (127.5f)),
			ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("max_note") + 0.5f), cursorScreenPos.y + winSize.y + 32),
			IM_COL32(0, 0, 255, 100)
		);
		ImGui::GetCurrentWindow()->DrawList->AddRectFilled(
			ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("root_note") - 0.5f), cursorScreenPos.y + winSize.y+32),
			ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("root_note") + 0.5f), cursorScreenPos.y + (winSize.y / 127) * (-0.5f)),
			IM_COL32(255, 0, 0, 100)
		);
	}
	ImGui::GetCurrentWindow()->DrawList->AddRect(
		ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("min_note")-0.5f), cursorScreenPos.y + (winSize.y / 127) * (127 - (drawZone->getInt("min_velocity") - 0.5f))),
		ImVec2(cursorScreenPos.x + (winSize.x / 127) * (drawZone->getInt("max_note") + 0.5f), cursorScreenPos.y + (winSize.y / 127) * (127 - (drawZone->getInt("max_velocity") + 0.5f))),
		IM_COL32(255, 127, 0, 255)
	);
	for (int i = 0; i < 4; i++) {
		if (zoneIdx == currentKeyzone) {
			if (hasCornerSelected && selectedCorner == i) {
				ImVec2 mousePos = ImGui::GetMousePos();
				int noteVal = std::clamp(((mousePos.x - (cursorScreenPos.x)) / winSize.x) * 127, 0.0f, 127.0f);
				int velocityVal = std::clamp(127 - ((mousePos.y - (cursorScreenPos.y)) / winSize.y)*127, 0.0f, 127.0f);
				
				switch (selectedCorner) {
				case 0:
					if (noteVal <= drawZone->getInt("max_note")) drawZone->getInt("min_note") = noteVal;
					if (velocityVal <= drawZone->getInt("max_velocity")) drawZone->getInt("min_velocity") = velocityVal;
					ImGui::BeginTooltip();
					ImGui::Text(std::string("Note " + std::to_string(drawZone->getInt("min_note"))).c_str());
					ImGui::Text(std::string("Velocity " + (std::to_string(fcsc_cfg.usePercentVelocity ? (int)((drawZone->getInt("min_velocity")/127.0f)*100) : drawZone->getInt("min_velocity")))).c_str());
					ImGui::EndTooltip();
					break;
				case 1:
					if (noteVal >= drawZone->getInt("min_note")) drawZone->getInt("max_note") = noteVal;
					if (velocityVal <= drawZone->getInt("max_velocity")) drawZone->getInt("min_velocity") = velocityVal;
					ImGui::BeginTooltip();
					ImGui::Text(std::string("Note " + std::to_string(drawZone->getInt("max_note"))).c_str());
					ImGui::Text(std::string("Velocity " + (std::to_string(fcsc_cfg.usePercentVelocity ? (int)((drawZone->getInt("min_velocity") / 127.0f) * 100) : drawZone->getInt("min_velocity")))).c_str());
					ImGui::EndTooltip();
					break;
				case 2:
					if (noteVal >= drawZone->getInt("min_note")) drawZone->getInt("max_note") = noteVal;
					if (velocityVal >= drawZone->getInt("min_velocity")) drawZone->getInt("max_velocity") = velocityVal;
					ImGui::BeginTooltip();
					ImGui::Text(std::string("Note " + std::to_string(drawZone->getInt("max_note"))).c_str());
					ImGui::Text(std::string("Velocity " + (std::to_string(fcsc_cfg.usePercentVelocity ? (int)((drawZone->getInt("max_velocity") / 127.0f) * 100) : drawZone->getInt("max_velocity")))).c_str());
					ImGui::EndTooltip();
					break;
				case 3:
					if (noteVal <= drawZone->getInt("max_note")) drawZone->getInt("min_note") = noteVal;
					if (velocityVal >= drawZone->getInt("min_velocity")) drawZone->getInt("max_velocity") = velocityVal;
					ImGui::BeginTooltip();
					ImGui::Text(std::string("Note " + std::to_string(drawZone->getInt("min_note"))).c_str());
					ImGui::Text(std::string("Velocity " + (std::to_string(fcsc_cfg.usePercentVelocity ? (int)((drawZone->getInt("max_velocity") / 127.0f) * 100) : drawZone->getInt("max_velocity")))).c_str());
					ImGui::EndTooltip();
					break;
				}
			}
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
				hasCornerSelected = false;
			}
			if ((hasCornerSelected && selectedCorner==i) || ImGui::IsMouseHoveringRect(ImVec2(corners[i].x - 5, corners[i].y - 5), ImVec2(corners[i].x + 5, corners[i].y + 5)))
			{	
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hasCornerSelected) {
					hasCornerSelected = true;
					selectedCorner = i;
				}
				ImGui::GetCurrentWindow()->DrawList->AddRectFilled(
					ImVec2(corners[i].x - 5, corners[i].y - 5),
					ImVec2(corners[i].x + 5, corners[i].y + 5),
					IM_COL32(255, 255, 127, 255)
				);
			}
			else
			{
				ImGui::GetCurrentWindow()->DrawList->AddRectFilled(
					ImVec2(corners[i].x - 5, corners[i].y - 5),
					ImVec2(corners[i].x + 5, corners[i].y + 5),
					IM_COL32(255, 127, 0, 255)
				);
			}
		}
	}
}


void display_keyzone_settings(hmx_fusion_nodes* keyzone, std::vector<HmxAudio::PackageFile*> moggFiles, hmx_fusion_nodes* audioLabels, hmx_fusion_nodes* map) {
	static bool keyzoneVisualEdit = false;

	if (keyzoneVisualEdit) {
		ImGui::SetNextWindowSize(ImVec2(512, 266+32), ImGuiCond_FirstUseEver);
		ImGui::Begin("Visual Keyzone Editor##VKZWIN", &keyzoneVisualEdit);
		ImVec2 winSize = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
		ImGui::BeginChild("##RectDraw", winSize);
		winSize = ImVec2(ImGui::GetContentRegionAvail().x - 60, ImGui::GetContentRegionAvail().y - 52);
		ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
		cursorScreenPos.x += 40;
		cursorScreenPos.y += 10;
		static bool noteClick = false;
		static bool velClick = false;
		static ImVec2 clickPos(0, 0);
		static int noteValStart = 0;
		static int velStart = 0;
		std::vector<std::string> noteNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		for (int i = 0; i < 128; i++) {
			bool sharp = false;
			int octKey = i % 12;
			int oct = (i / 12)-2;
			if (octKey == 1 || octKey == 3 || octKey == 6 || octKey == 8 || octKey == 10) sharp = true;
			ImVec2 topLeft(cursorScreenPos.x + (winSize.x / 127) * (i - 0.5f), cursorScreenPos.y + (winSize.y / 127) * (127.5f));
			ImVec2 bottomRight(cursorScreenPos.x + (winSize.x / 127) * (i + 0.5f), cursorScreenPos.y + winSize.y + 32);
			ImGui::GetCurrentWindow()->DrawList->AddRectFilled(
				topLeft,
				bottomRight,
				sharp ? IM_COL32(0, 0, 0, 255) : IM_COL32(255, 255, 255, 255)
			);
			if (ImGui::IsMouseHoveringRect(topLeft, bottomRight)) {
				ImGui::BeginTooltip();
				ImGui::Text(std::string(noteNames[octKey] + std::to_string(oct)).c_str());
				ImGui::EndTooltip();
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !noteClick && !velClick) {
					noteClick = true;
					clickPos = ImGui::GetMousePos();
					noteValStart = i;
				}
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
					clickPos = ImGui::GetMousePos();
					keyzone->getInt("root_note") = i;
				}
			}
		}
		for (int i = 0; i < 128; i++){
			ImVec2 topLeft(cursorScreenPos.x - 40 , cursorScreenPos.y + (winSize.y / 127) * (127 - i - 0.5f));
			ImVec2 bottomRight(cursorScreenPos.x - (winSize.x / 127) * 0.5f, cursorScreenPos.y + (winSize.y / 127) * (127 - i + 0.5f));
			ImGui::GetCurrentWindow()->DrawList->AddRectFilled(
				topLeft,
				bottomRight,
				IM_COL32(i*2, 255-(i*2), 0, 255)
			);
			if (ImGui::IsMouseHoveringRect(topLeft, bottomRight)) {
				ImGui::BeginTooltip();
				ImGui::Text(std::to_string(fcsc_cfg.usePercentVelocity? (int)((i/127.0f)*100):i).c_str());
				ImGui::EndTooltip();
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !noteClick && !velClick) {
					velClick = true;
					clickPos = ImGui::GetMousePos();
					velStart = i;
				}
			}
		}
		if (noteClick) {
			ImVec2 mousePos = ImGui::GetMousePos();
			int noteVal = std::clamp(((mousePos.x - (cursorScreenPos.x)) / winSize.x) * 127, 0.0f, 127.0f);
			if (noteVal >= noteValStart) {
				keyzone->getInt("min_note") = noteValStart;
				keyzone->getInt("max_note") = noteVal;
			}
			else {
				keyzone->getInt("min_note") = noteVal;
				keyzone->getInt("max_note") = noteValStart;
			}
		}
		if (velClick) {
			ImVec2 mousePos = ImGui::GetMousePos();
			int velVal = std::clamp(127-((mousePos.y - (cursorScreenPos.y)) / winSize.y) * 127, 0.0f, 127.0f);
			if (velVal >= velStart) {
				keyzone->getInt("min_velocity") = velStart;
				keyzone->getInt("max_velocity") = velVal;
			}
			else {
				keyzone->getInt("min_velocity") = velVal;
				keyzone->getInt("max_velocity") = velStart;
			}
		}
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			noteClick = false;
			noteValStart = 0;
			velClick = false;
			velStart = 0;
			clickPos = ImVec2(0, 0);
		}

		
		int zoneIdx = 0;
		for (auto& zone : map->children) {
			if (zoneIdx != currentKeyzone) {
				hmx_fusion_nodes* drawZone = std::get<hmx_fusion_nodes*>(zone.value);
				draw_visual_keymap_rect(drawZone, cursorScreenPos, winSize, zoneIdx);
			}
			zoneIdx++;
		}
		draw_visual_keymap_rect(keyzone, cursorScreenPos, winSize, currentKeyzone);
		ImGui::EndChild();
		ImGui::End();
	}
	
	int itemWidth = 300;
	ImGui::PushItemWidth(itemWidth);
	if(ImGui::InputText("Keymap Label", &keyzone->getString("zone_label")))
		unsavedChanges = true;
	bool unp = keyzone->getInt("unpitched") == 1;
	bool unp_changed = ImGui::Checkbox("Unpitched", &unp);
	if (unp_changed) {
		unsavedChanges = true;
		if (unp)
			keyzone->getInt("unpitched") = 1;
		else
			keyzone->getInt("unpitched") = 0;
	}


	auto&& ts = keyzone->getNode("timestretch_settings");
	if (ts.getChild("orig_tempo_sync") == nullptr) {
		hmx_fusion_node label;
		label.key = "orig_tempo_sync";
		label.value = 1;
		ts.children.insert(ts.children.begin(), label);
	}
	bool orig_tempo_sync = ts.getInt("orig_tempo_sync") == 1;
	bool natp = ts.getInt("maintain_formant") == 1;
	bool natp_changed = ImGui::Checkbox("Natural Pitching", &natp);
	if (natp_changed) {
		unsavedChanges = true;
		if (natp)
			ts.getInt("maintain_formant") = 1;
		else
			ts.getInt("maintain_formant") = 0;
	}




	std::vector<std::string> fileNames;
	for (auto mogg : moggFiles) {
		fileNames.emplace_back(moggName(mogg->fileName));
	}
	auto it = std::find(fileNames.begin(), fileNames.end(), moggName(keyzone->getString("sample_path")));
	if (it != fileNames.end())
		selectedAudioFile = std::distance(fileNames.begin(), it);
	else
		selectedAudioFile = 0;


	if (ImGui::BeginCombo("Audio File", (std::to_string(selectedAudioFile) + " - " + audioLabels->getString(fileNames[selectedAudioFile])).c_str())) {
		for (int i = 0; i < fileNames.size(); ++i)
		{
			bool is_selected = (selectedAudioFile == i);
			if (ImGui::Selectable((std::to_string(i) +" - "+ audioLabels->getString(fileNames[i])).c_str(), is_selected))
			{
				selectedAudioFile = i;
				keyzone->getString("sample_path") = "C:/"+fileNames[i]+".mogg";
			}
			if (is_selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::PopItemWidth();
	struct KeymapPreset {
		int min = 0;
		int max = 71;
		int root = 60;
	};

	KeymapPreset kpmaj;

	KeymapPreset kpmin;
	kpmin.min = 72;
	kpmin.max = 127;
	kpmin.root = 84;

	KeymapPreset kpshr;
	kpshr.min = 0;
	kpshr.max = 127;
	kpshr.root = 60;



	int selectedPreset = keyzone->getInt("keymap_preset");

	const char* options[] = { "Major", "Minor","Shared","Custom" };
	if (ImGui::BeginCombo("Keymap Preset", options[selectedPreset])) {
		for (int i = 0; i < 4; i++) {
			if (ImGui::Selectable(options[i])) {
				keyzone->getInt("keymap_preset") = i;
				unsavedChanges = true;
				if (i == 0) {
					keyzone->getInt("min_note") = kpmaj.min;
					keyzone->getInt("max_note") = kpmaj.max;
					keyzone->getInt("root_note") = kpmaj.root;
				}
				else if (i == 1) {
					keyzone->getInt("min_note") = kpmin.min;
					keyzone->getInt("max_note") = kpmin.max;
					keyzone->getInt("root_note") = kpmin.root;
				}
				else if (i == 2) {
					keyzone->getInt("min_note") = kpshr.min;
					keyzone->getInt("max_note") = kpshr.max;
					keyzone->getInt("root_note") = kpshr.root;
				}
				if (i != 3) {
					keyzone->getInt("min_velocity") = 0;
					keyzone->getInt("max_velocity") = 127;
					keyzone->getInt("start_offset_frame") = -1;
					keyzone->getInt("end_offset_frame") = -1;
				}
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::CollapsingHeader("Advanced Keymap Settings")) {
		int minvel = keyzone->getInt("min_velocity");
		int maxvel = keyzone->getInt("max_velocity");
		if (fcsc_cfg.usePercentVelocity) {
			minvel = minvel / 1.27;
			maxvel = maxvel / 1.27;
		}
		bool sng = keyzone->getInt("singleton") == 1;
		bool sng_changed = ImGui::Checkbox("Singleton", &sng);
		if (sng_changed) {
			unsavedChanges = true;
			if (sng)
				keyzone->getInt("singleton") = 1;
			else
				keyzone->getInt("singleton") = 0;
		}
		ImGui::SameLine();
		HelpMarker("If checked, the specified keyzone can only be triggered if it currently is not playing.");
		ImGui::SameLine();
		bool mt = ts.getInt("maintain_time") == 1;
		bool mt_changed = ImGui::Checkbox("Maintain Time", &mt);
		if (mt_changed) {
			unsavedChanges = true;
			if (mt)
				ts.getInt("maintain_time") = 1;
			else
				ts.getInt("maintain_time") = 0;
		}
		ImGui::SameLine();
		HelpMarker("Ensures that the timing of the audio does not change when the midi note played is not the root note.");
		ImGui::SameLine();
		bool st = ts.getInt("sync_tempo") == 1;
		bool st_changed = ImGui::Checkbox("Sync Tempo", &st);
		if (st_changed) {
			unsavedChanges = true;
			if (st)
				ts.getInt("sync_tempo") = 1;
			else
				ts.getInt("sync_tempo") = 0;
		}
		ImGui::SameLine();
		HelpMarker("Slows down or speeds up the audio with the tempo.");

		ImGui::TextWrapped("For audio to sync properly if it's meant to sync to the tempo and play on multiple notes, both Maintain Time and Sync Tempo have to be enabled.");

		bool ots_changed = ImGui::Checkbox("Sync orig_tempo to song tempo", &orig_tempo_sync);
		ImGui::SameLine();
		HelpMarker("If unchecked, will allow changing orig_tempo to a different value than the song's bpm, and the game will timestretch accordingly");
		if (ots_changed) {
			unsavedChanges = true;
			if (orig_tempo_sync) {
				ts.getInt("orig_tempo_sync") = 1;
			}
			else {
				ts.getInt("orig_tempo_sync") = 0;
			}
		}
		if (!orig_tempo_sync) {
			if (ImGui::InputScalar("Original Tempo", ImGuiDataType_U32, &ts.getInt("orig_tempo"))) {
				unsavedChanges = true;
			}
		}

		bool vel2vol = keyzone->getInt("velocity_to_volume") == 1;
		bool vel2vol_changed = ImGui::Checkbox("Velocity to Volume", &vel2vol);
		if (vel2vol_changed) {
			unsavedChanges = true;
			if (vel2vol)
				keyzone->getInt("velocity_to_volume") = 1;
			else
				keyzone->getInt("velocity_to_volume") = 0;
		}
		ImGui::SameLine();
		HelpMarker("If checked, the midi note velocity will control the volume of the sample");

		float& kzvol = keyzone->getFloat("volume");
		ImGui::PushItemWidth(150);
		if (ImGui::InputFloat("Volume", &kzvol, 0.0f, 0.0f, "%.2f"))
			unsavedChanges = true;
		ImGui::PopItemWidth();
		ImGui::SameLine();
		HelpMarker("The volume of the keyzone. 0 means it's the same volume as the imported audio, negative values make it quieter, positive values make it louder. This is relative to the gain for the disc/riser.");
		ImGui::SameLine();

		float& kzpan = keyzone->getNode("pan").getFloat("position");
		if (kzpan < -1)
			kzpan = -1;
		else if (kzpan > 1)
			kzpan = 1;
		ImGui::PushItemWidth(150);
		if (ImGui::InputFloat("Pan", &kzpan, 0.0f, 0.0f, "%.2f"))
			unsavedChanges = true;
		ImGui::PopItemWidth();
		ImGui::SameLine();
		HelpMarker("Panning of the keyzone. -1 is left, 1 is right, 0 is center");
		if(ImGui::Button("Open visual keyzone editor##VKZButton"))
			keyzoneVisualEdit=true;
		ImGui::Checkbox("Link MIDI Notes", &editAllMidiNote);
		ImGui::SameLine();
		HelpMarker("If checked, changing one midi note value will change all 3. Useful for drums.");
		ImGui::PushItemWidth(itemWidth);

		if (ImGui::InputScalar("Map - Min Note", ImGuiDataType_U32, &keyzone->getInt("min_note"))) {
			unsavedChanges = true;
			selectedPreset = 3;
			keyzone->getInt("keymap_preset") = 3;
			if (editAllMidiNote) {
				keyzone->getInt("max_note") = std::clamp(keyzone->getInt("min_note"), 0, 127);
				keyzone->getInt("root_note") = std::clamp(keyzone->getInt("min_note"), 0, 127);
			}
			keyzone->getInt("min_note") = std::clamp(keyzone->getInt("min_note"), 0, 127);
		}

		ImGui::SameLine();
		HelpMarker("The lowest midi note that the selected sample will play.");

		if (ImGui::InputScalar("Map - Highest Note", ImGuiDataType_U32, &keyzone->getInt("max_note"))) {
			unsavedChanges = true;
			selectedPreset = 3;
			keyzone->getInt("keymap_preset") = 3;
			if (editAllMidiNote) {
				keyzone->getInt("min_note") = std::clamp(keyzone->getInt("max_note"), 0, 127);
				keyzone->getInt("root_note") = std::clamp(keyzone->getInt("max_note"), 0, 127);
			}
			keyzone->getInt("max_note") = std::clamp(keyzone->getInt("max_note"), 0, 127);
		}
		ImGui::SameLine();
		HelpMarker("The highest midi note that the selected sample will play.");

		if (ImGui::InputScalar("Map - Root Note", ImGuiDataType_U32, &keyzone->getInt("root_note"))) {
			unsavedChanges = true;
			selectedPreset = 3;
			keyzone->getInt("keymap_preset") = 3;
			if (editAllMidiNote) {
				keyzone->getInt("min_note") = std::clamp(keyzone->getInt("root_note"), 0, 127);
				keyzone->getInt("max_note") = std::clamp(keyzone->getInt("root_note"), 0, 127);
			}
			keyzone->getInt("root_note") = std::clamp(keyzone->getInt("root_note"), 0, 127);
		}
		ImGui::SameLine();
		HelpMarker("The note at which that the selected sample will play at its original pitch.");

		if (ImGui::InputScalar("Map - Min Velocity", ImGuiDataType_U32, &minvel)) {
			unsavedChanges = true;
			selectedPreset = 3;
			keyzone->getInt("keymap_preset") = 3;
			if (fcsc_cfg.usePercentVelocity)
				keyzone->getInt("min_velocity") = std::ceil(std::clamp(minvel, 0, 100) * 1.27);
			else
				keyzone->getInt("min_velocity") = std::clamp(minvel, 0, 127);
		}
		ImGui::SameLine();
		HelpMarker("The lowest midi note velocity at which the selected sample will play.");

		if (ImGui::InputScalar("Map - Max Velocity", ImGuiDataType_U32, &maxvel)) {
			unsavedChanges = true;
			selectedPreset = 3;
			keyzone->getInt("keymap_preset") = 3;
			if (fcsc_cfg.usePercentVelocity)
				keyzone->getInt("max_velocity") = std::ceil(std::clamp(maxvel, 0, 100) * 1.27);
			else
				keyzone->getInt("max_velocity") = std::clamp(maxvel, 0, 127);
		}
		ImGui::SameLine();
		HelpMarker("The highest midi note velocity at which the selected sample will play.");

		if (ImGui::InputScalar("Audio - Start Offset", ImGuiDataType_S32, &keyzone->getInt("start_offset_frame"))) {
			unsavedChanges = true;
			selectedPreset = 3;
			keyzone->getInt("keymap_preset") = 3;
			keyzone->getInt("start_offset_frame") = std::clamp(keyzone->getInt("start_offset_frame"), -1, INT_MAX);
		}
		ImGui::SameLine();
		HelpMarker("The offset, in samples, that the audio will start playing from.");

		if (ImGui::InputScalar("Audio - End Offset", ImGuiDataType_S32, &keyzone->getInt("end_offset_frame"))) {
			unsavedChanges = true;
			selectedPreset = 3;
			keyzone->getInt("keymap_preset") = 3;
			keyzone->getInt("end_offset_frame") = std::clamp(keyzone->getInt("end_offset_frame"), -1, INT_MAX);
		}
		ImGui::SameLine();
		HelpMarker("The offset, in samples, that the audio will stop playing at.");

		ImGui::PopItemWidth();
	}
}



bool midi_error = false;
std::string mfrError;
std::string last_import_midi;

static void display_fusionmidisettings(HmxAssetFile& asset, CelData& celData, HmxAudio::PackageFile*& fusionPackageFile, std::vector<HmxAudio::PackageFile*>& moggFiles, bool disc_advanced, int disc_midi_maj_single, int disc_midi_min_single, bool isRiser = false)
{
	ImVec2 btnHolderSize = ImVec2(ImGui::GetContentRegionAvail().x / 2, 30);
	ImVec2 btnHolderSizeWarn = btnHolderSize;
	btnHolderSizeWarn.y += 20;
	ImGui::Text("Fusion File");
	ImGui::BeginChild("btnL1", btnHolderSize);
	if (ImGui::Button("Export##FUSIONEXPORT", btnHolderSize)) {
		auto file = SaveFile("Fusion Text File (.fusion)\0*.fusion\0", "fusion", "");
		if (file) {
			for (auto&& f : asset.audio.audioFiles) {
				if (f.fileType == "FusionPatchResource") {

					std::ofstream outFile(*file);
					std::string outStr = hmx_fusion_parser::outputData(std::get<HmxAudio::PackageFile::FusionFileResource>(f.resourceHeader).nodes);
					outFile << outStr;

					break;
				}
			}
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("btnR1", btnHolderSize);
	if (ImGui::Button("Import##FUSIONIMPORT", btnHolderSize)) {
		auto file = OpenFile("Fusion Text File (.fusion)\0*.fusion\0");
		if (file) {
			unsavedChanges = true;
			for (auto&& f : asset.audio.audioFiles) {
				if (f.fileType == "FusionPatchResource") {
					std::ifstream infile(*file, std::ios_base::binary);
					std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
					std::get<HmxAudio::PackageFile::FusionFileResource>(f.resourceHeader).nodes = hmx_fusion_parser::parseData(fileData);
					if (std::get<HmxAudio::PackageFile::FusionFileResource>(f.resourceHeader).nodes.getNode("keymap").children.size() > 2)
						disc_advanced = true;
					break;
				}
			}
		}
	}
	ImGui::EndChild();
	ImGui::Spacing();
	ImGui::Text("Overwrite MIDI");
	bool overwrite_midi = false;
	bool maj = true;
	ImGui::BeginChild("btnL2", btnHolderSize);
	if (ImGui::Button("Major##OVERWRITEMAJOR", btnHolderSize)) {
		overwrite_midi = true;
	}
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("btnR2", btnHolderSize);
	if (ImGui::Button("Minor##OVERWRITEMINOR", btnHolderSize)) {
		overwrite_midi = true;
		maj = false;
	}
	ImGui::EndChild();
	if (overwrite_midi) {
		try {
			auto file = OpenFile("MIDI (.mid)\0*.mid\0Harmonix Midi Resource File (.mid_pc)\0*.mid_pc\0");
			if (file) {
				unsavedChanges = true;
				AssetLink<MidiSongAsset>* midiSong = nullptr;
				if (maj) {
					midiSong = isRiser ? &celData.songTransitionFile.data.majorAssets[0] : &celData.majorAssets[0];
				}
				else {
					midiSong = isRiser ? &celData.songTransitionFile.data.minorAssets[0] : &celData.minorAssets[0];
				}

				auto&& midi_file = midiSong->data.midiFile.data;
				auto&& midiAsset = std::get<HmxAssetFile>(midi_file.file.e->getData().data.catagoryValues[0].value);

				HmxAudio::PackageFile::MidiFileResource mfr;

				if (!maj) {
					mfr.minor = true;
				}
				if (endsWith(*file, ".mid")) {
					mfr.MFR_from_midi(*file);
				}
				else if (endsWith(*file, ".mid_pc")) {
					mfr.MFRImport(*file);
				}

				if (mfr.magic == 2) {
					{
						for (auto&& file : asset.audio.audioFiles) {
							if (file.fileType == "FusionPatchResource") {
								fusionPackageFile = &file;
							}
							else if (file.fileType == "MoggSampleResource") {
								moggFiles.emplace_back(&file);
							}
						}

						auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
						mfr.is_single_note = mfr.MFR_is_single_note();

					}
					midiAsset.audio.audioFiles[0].resourceHeader = std::move(mfr);
					midi_error = false;
					mfrError = "";
				}
				else {
					last_import_midi = celData.type.getString() + (isRiser ? " Riser" : " Disc") + (maj ? " Major" : " Minor" );
					midi_error = true;
					if (mfr.magic == 0) {
						mfrError = "MIDI file does not contain 'samplemidi' track";
					}
					else if (mfr.magic == 480) {
						mfrError = "Ticks per quarter note is not set to 480";
					}
					
				}
			}
		}
		catch (const std::exception& ex) {
			last_import_midi = celData.type.getString() + (isRiser ? " Riser" : " Disc") + (maj ? " Major" : " Minor");
			midi_error = true;
			mfrError = ex.what();
		}
		

	}
	if (midi_error) {
		ImGui::TextWrapped(("Error importing " + last_import_midi + " MIDI: " + mfrError).c_str());
	}
	ImGui::Spacing();

	ImGui::Text("Export MIDI");
	bool export_midi = false;

	ImGui::BeginChild("btnL3", btnHolderSizeWarn);
	if (ImGui::Button("Major##EXPORTMAJOR", btnHolderSize)) {
		export_midi = true;
		maj = true;
	}
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("btnR3", btnHolderSizeWarn);
	if (ImGui::Button("Minor##EXPORTMINOR", btnHolderSize)) {
		export_midi = true;
		maj = false;
	}
	ImGui::EndChild();
	if (export_midi) {
		try {

			auto file = SaveFile("MIDI (.mid)\0*.mid\0", "mid", "");
			if (file) {
				AssetLink<MidiSongAsset>* midiSong = nullptr;
				if (maj) {
					midiSong = isRiser ? &celData.songTransitionFile.data.majorAssets[0] : &celData.majorAssets[0];
				}
				else {
					midiSong = isRiser ? &celData.songTransitionFile.data.minorAssets[0] : &celData.minorAssets[0];
				}

				auto&& midi_file = midiSong->data.midiFile.data;
				auto&& midiAsset = std::get<HmxAssetFile>(midi_file.file.e->getData().data.catagoryValues[0].value);
				std::get<HmxAudio::PackageFile::MidiFileResource>(midiAsset.audio.audioFiles[0].resourceHeader).MFR_to_midi(*file);
			}
		}
		catch (const std::exception& ex) {
			midi_error = true;
			mfrError = ex.what();
		}
	}
	if (!isRiser) {
		bool tickLengthAdvanced = celData.tickLengthAdvanced;
		if (disc_advanced) {
			if(ImGui::Checkbox("Advanced Length Input", &celData.tickLengthAdvanced))
				unsavedChanges = true;
			ImGui::SameLine();
			HelpMarker("Will allow the length in ticks for the custom to loop to be set to any value. Calculate using the formula \"Length = 480 * beats\". Default is 61440, which is the length of 32 bars in midi ticks.");
		}
		if (tickLengthAdvanced) {
			if(ImGui::InputInt("Tick Length", &celData.tickLength, 0, 0))
				unsavedChanges = true;
		}
		else {
			const char* options[] = { "8 bars", "16 bars","32 bars","64 bars" };
			if (ImGui::BeginCombo("Disc Length", options[celData.selectedTickLength])) {
				for (int i = 0; i < 4; i++) {
					if (ImGui::Selectable(options[i])) {
						unsavedChanges = true;
						celData.selectedTickLength = i;
						if (i == 0) {
							celData.tickLength = 15360;
						}
						else if (i == 1) {
							celData.tickLength = 30720;
						}
						else if (i == 2) {
							celData.tickLength = 61440;
						}
						else if (i == 3) {
							celData.tickLength = 122880;
						}
					}
				}
				ImGui::EndCombo();
			}
		}
		if (ImGui::Button("Update MIDI length")) {
			unsavedChanges = true;
			if (disc_midi_maj_single == 1) {
				AssetLink<MidiSongAsset>& midiSong = celData.majorAssets[0];
				auto&& midi_file = midiSong.data.midiFile.data;
				auto&& midiAsset = std::get<HmxAudio::PackageFile::MidiFileResource>(std::get<HmxAssetFile>(midi_file.file.e->getData().data.catagoryValues[0].value).audio.audioFiles[0].resourceHeader);
				midiAsset.final_tick = celData.tickLength;
				midiAsset.final_tick_minus_one = celData.tickLength - 1;
				midiAsset.last_track_final_tick = celData.tickLength;
				for (auto& track : midiAsset.tracks) {
					if (track.strings[track.trackname_str_idx] == "samplemidi") {
						int midi_idx = 0;
						for (auto& mfrevent : track.events) {
							if (mfrevent.event_type == 1) {
								if (midi_idx == 1) {
									mfrevent.tick = celData.tickLength;
								}
								midi_idx++;
							}
						}
					}
				}
			}
			if (disc_midi_min_single == 1) {
				AssetLink<MidiSongAsset>& midiSong = celData.minorAssets[0];
				auto&& midi_file = midiSong.data.midiFile.data;
				auto&& midiAsset = std::get<HmxAudio::PackageFile::MidiFileResource>(std::get<HmxAssetFile>(midi_file.file.e->getData().data.catagoryValues[0].value).audio.audioFiles[0].resourceHeader);
				midiAsset.final_tick = celData.tickLength;
				midiAsset.final_tick_minus_one = celData.tickLength - 1;
				midiAsset.last_track_final_tick = celData.tickLength;
				for (auto& track : midiAsset.tracks) {
					if (track.strings[track.trackname_str_idx] == "samplemidi") {
						int midi_idx = 0;
						for (auto& mfrevent : track.events) {
							if (mfrevent.event_type == 1) {
								if (midi_idx == 1) {
									mfrevent.tick = celData.tickLength - 1;
								}
								midi_idx++;
							}
						}
					}
				}
			}
		}
		if (disc_midi_maj_single != 1) {
			ImGui::Text("Major MIDI length cannot be automatically updated");
		}
		if (disc_midi_min_single != 1) {
			ImGui::Text("Minor MIDI length cannot be automatically updated");
		}
	}
	
}

const char* chordNamesMinorMajor[] = { "1m", "2mb5", "b3", "4m", "5m", "b6", "b7", "sep", "1", "2m", "3m", "4", "5", "6m", "sep", "b2" };
const char* chordNamesMajorMinor[] = { "1", "2m", "3m", "4", "5", "6m", "sep", "1m", "2mb5", "b3", "4m", "5m", "b6", "b7", "sep", "b2" };
const char* chordNamesInterleaved[] = { "1", "1m", "2m", "2mb5", "3m", "b3", "4", "4m", "5", "5m", "6m", "b6", "b7", "b2" };

static std::vector<HmxAudio::PackageFile::MidiFileResource::Chord> convertChordsMode(std::vector<HmxAudio::PackageFile::MidiFileResource::Chord> chords, bool minor) {
	for (auto& chd : chords) {
		if (fcsc_cfg.swapBorrowedChords) {
			if (chd.name == "1")
				chd.name = "1m";
			else if (chd.name == "1m")
				chd.name = "1";
			else if (chd.name == "2m")
				chd.name = "2mb5";
			else if (chd.name == "2mb5")
				chd.name = "2m";
			else if (chd.name == "3m")
				chd.name = "b3";
			else if (chd.name == "b3")
				chd.name = "3m";
			else if (chd.name == "4")
				chd.name = "4m";
			else if (chd.name == "4m")
				chd.name = "4";
			else if (chd.name == "5m" || chd.name == "b7")
				chd.name = "5";
			else if (chd.name == "5")
				chd.name = "5m";
			else if (chd.name == "b6")
				chd.name = "6m";
			else if (chd.name == "6m")
				chd.name = "b6";
			else {
				if (chd.name != "b2") {
					if (minor)
						chd.name = "1m";
					else
						chd.name = "1";
				}
			}
		}
		else {
			if (minor) {
				if (chd.name == "1")
					chd.name = "1m";
				else if (chd.name == "2m")
					chd.name = "2mb5";
				else if (chd.name == "3m")
					chd.name = "b3";
				else if (chd.name == "4")
					chd.name = "4m";
				else if (chd.name == "5")
					chd.name = "5m";
				else if (chd.name == "6m")
					chd.name = "b6";
				else {
					if (chd.name != "1m" &&
						chd.name != "2mb5" &&
						chd.name != "b3" &&
						chd.name != "4m" &&
						chd.name != "5m" &&
						chd.name != "b6" &&
						chd.name != "b7" &&
						chd.name != "b2") {
						chd.name = "1m";
					}
				}
			}
			else {
				if (chd.name == "1m")
					chd.name = "1";
				else if (chd.name == "2mb5")
					chd.name = "2m";
				else if (chd.name == "b3")
					chd.name = "3m";
				else if (chd.name == "4m")
					chd.name = "4";
				else if (chd.name == "5m" || chd.name == "b7")
					chd.name = "5";
				else if (chd.name == "b6")
					chd.name = "6m";
				else {
					if (chd.name != "1" &&
						chd.name != "2m" &&
						chd.name != "3m" &&
						chd.name != "4" &&
						chd.name != "5" &&
						chd.name != "6m" &&
						chd.name != "b2") {
						chd.name = "1";
					}
				}
			}
		}
		
	}
	return chords;
}

std::vector<std::string> layer_select_modes = {"layers","random","random_with_repetition","cycle"};

static void display_cel_audio_options(CelData& celData, HmxAssetFile& asset, std::vector<HmxAudio::PackageFile*>& moggFiles, FusionFileAsset& fusionFile, HmxAudio::PackageFile* fusionPackageFile, bool duplicate_moggs, bool isRiser = false)
{
	if (isRiser)
		curCelTabOffset = 1;
	else
		curCelTabOffset = 0;



	if (curCelTab + curCelTabOffset != lastCelTab) {
		currentAudioFile = 0;
		currentKeyzone = 0;
		selectedAudioFile = 0;
		curPickup = -1;
		curChord = -1;
		pickupInput = 0;
		chordInput = 0;
		chordInputTicks = 0;
		lastCelTab = curCelTab + curCelTabOffset;
	}

	std::string celShortName = celData.shortName + (isRiser ? "_trans" : "");
	auto aRegion = ImGui::GetContentRegionAvail();

	auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
	auto& map = fusion.nodes.getNode("keymap");
	if (fusion.nodes.getChild("audio_labels") == nullptr) {
		hmx_fusion_node audiolabelholder;
		audiolabelholder.key = "audio_labels";
		audiolabelholder.value = new hmx_fusion_nodes;
		auto& alnodes = std::get<hmx_fusion_nodes*>(audiolabelholder.value);
		int idx = 0;
		for (auto& mogg : moggFiles) {
			hmx_fusion_node audiolabel;
			audiolabel.key = moggName(mogg->fileName);
			audiolabel.value = moggName(mogg->fileName);
			alnodes->children.emplace_back(audiolabel);
		}
		fusion.nodes.children.insert(fusion.nodes.children.begin(), audiolabelholder);
		AssetLink<MidiSongAsset>* midiSong = nullptr;
		{
			midiSong = &celData.majorAssets[0];
			auto&& midi_file = midiSong->data.midiFile.data;
			auto&& midiAsset = std::get<HmxAssetFile>(midi_file.file.e->getData().data.catagoryValues[0].value);
			auto& mfr = std::get<HmxAudio::PackageFile::MidiFileResource>(midiAsset.audio.audioFiles[0].resourceHeader);
		}

		{
			midiSong = &celData.minorAssets[0];
			auto&& midi_file = midiSong->data.midiFile.data;
			auto&& midiAsset = std::get<HmxAssetFile>(midi_file.file.e->getData().data.catagoryValues[0].value);
			auto& mfr = std::get<HmxAudio::PackageFile::MidiFileResource>(midiAsset.audio.audioFiles[0].resourceHeader);
		}
		
	}
	auto& audiolabels = fusion.nodes.getNode("audio_labels");
	bool advanced = fusion.nodes.getInt("edit_advanced") == 1;
	std::string advBtn = "Switch to Advanced Mode";
	if (advanced)
		advBtn = "Switch to Simple Mode";
	std::string riserText = isRiser ? "riser" : "disc";
	std::string gainInputLabel = std::string(isRiser ? "Riser" : "Disc") + " Gain";
	std::string gainHelpString = "The gain of the " + riserText + " in dB. If the audio is too loud, decrease the gain. If it's too quiet, increase the gain. 0.00 dB is the default. Thie affects the volume of the whole " + riserText + ", if only one audio file is too quiet/too loud, the volume has to be adjusted for that audio file in your DAW.";
	float& trackGain = std::get<hmx_fusion_nodes*>(fusion.nodes.getNode("presets").children[0].value)->getFloat("volume");
	ImGui::PushItemWidth(150);
	if (ImGui::InputFloat(gainInputLabel.c_str(), &trackGain, 0.0f, 0.0f, "%.2f"))
		unsavedChanges = true;
	ImGui::PopItemWidth();
	ImGui::SameLine();

	HelpMarker(gainHelpString.c_str());
	if (advanced) {
		ImGui::SameLine();
		ImGui::PushItemWidth(150);
		std::string& layerMode = std::get<hmx_fusion_nodes*>(fusion.nodes.getNode("presets").children[0].value)->getString("layer_select_mode");
		if (std::find(layer_select_modes.begin(), layer_select_modes.end(), layerMode) == layer_select_modes.end())
			layerMode = "layers";
		if (ImGui::BeginCombo("Layering Mode", layerMode.c_str())) {
			for (int i = 0; i < layer_select_modes.size(); ++i)
			{
				bool is_selected = layerMode == layer_select_modes[i];
				if (ImGui::Selectable(layer_select_modes[i].c_str(), is_selected))
				{
					std::get<hmx_fusion_nodes*>(fusion.nodes.getNode("presets").children[0].value)->getString("layer_select_mode") = layer_select_modes[i];
				}
				if (is_selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopItemWidth();
		ImGui::SameLine();
		HelpMarker("Layering mode determines how the keyzones will be played. The modes are as follows:\nLayers - Any overlapping keyzones will play at the same time\nRandom - Randomly select a keyzone from any on the same note and velocity\nRandom with repetition: Same as Random, but the last played keyzone can repeat\nCycle: Will play the keyzones on the same note in a cyling pattern.");
	}
	if (ImGui::BeginPopupModal("Switch Modes?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::BeginChild("PopupHolder", ImVec2(420, 120));
		if (advanced) {
			ImGui::BeginChild("Text", ImVec2(420, 85));
			ImGui::Text("Are you sure you want to switch to Simple Mode?");
			ImGui::TextWrapped("WARNING: This will reset audio file and keymap count, as well as the keymap midi, velocity, and offset settings.");
			ImGui::EndChild();
			ImGui::BeginChild("Buttons", ImVec2(420, 25));
			if (ImGui::Button("Yes", ImVec2(120, 0)))
			{
				unsavedChanges = true;
				ImGui::CloseCurrentPopup();

				currentAudioFile = 0;
				currentKeyzone = 0;
				std::unordered_set<std::string> usedFileNames;
				usedFileNames.emplace(std::get<hmx_fusion_nodes*>(map.children[0].value)->getString("sample_path"));
				if (map.children.size() >= 2) {
					usedFileNames.emplace(std::get<hmx_fusion_nodes*>(map.children[1].value)->getString("sample_path"));
				}

				int audioFileCount = 0;
				for (auto it = asset.audio.audioFiles.begin(); it != asset.audio.audioFiles.end();) {
					if (it->fileType == "MoggSampleResource") {
						if (audioFileCount == 2 || usedFileNames.count(it->fileName) == 0) {
							it = asset.audio.audioFiles.erase(it);
						}
						else {
							it->fileName = "C:/" + celShortName + "_" + std::to_string(audioFileCount) + ".mogg";
							audioFileCount++;
							++it;
						}
					}
					else {
						++it;
					}
				}
				if (map.children.size() == 1) {
					map.children.emplace_back(map.children[0]);
					std::string str = hmx_fusion_parser::outputData(map);
					std::vector<std::uint8_t> vec(str.begin(), str.end());
					map = hmx_fusion_parser::parseData(vec);
				}
				else if (map.children.size() > 2)
					map.children.resize(2);

				moggFiles.clear();
				fusionFile.playableMoggs.resize(audioFileCount);
				for (auto&& file : asset.audio.audioFiles) {
					if (file.fileType == "FusionPatchResource") {
						fusionPackageFile = &file;
					}
					else if (file.fileType == "MoggSampleResource") {
						moggFiles.emplace_back(&file);
					}
				}


				std::vector<hmx_fusion_nodes*>nodes;
				nodes.emplace_back(std::get<hmx_fusion_nodes*>(map.children[0].value));
				nodes.emplace_back(std::get<hmx_fusion_nodes*>(map.children[1].value));


				nodes[0]->getString("zone_label") = "Major";
				nodes[0]->getInt("min_note") = 0;
				nodes[0]->getInt("root_note") = 60;
				nodes[0]->getInt("max_note") = 71;
				nodes[1]->getString("zone_label") = "Minor";
				nodes[1]->getInt("min_note") = 72;
				nodes[1]->getInt("root_note") = 84;
				nodes[1]->getInt("max_note") = 127;
				int idx = 0;
				for (auto c : nodes) {
					c->getInt("min_velocity") = 0;
					c->getInt("max_velocity") = 127;
					c->getInt("start_offset_frame") = -1;
					c->getInt("end_offset_frame") = -1;
					c->getString("sample_path") = moggFiles[idx]->fileName;
					if (moggFiles.size() != 1) {
						idx++;
					}
				}

				auto&& ts = nodes[0]->getNode("timestretch_settings");
				auto&& ts2 = nodes[1]->getNode("timestretch_settings");

				ts.getInt("maintain_time") = 1;
				ts2.getInt("maintain_time") = 1;
				ts.getInt("sync_tempo") = 1;
				ts2.getInt("sync_tempo") = 1;
				auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
				fusion.nodes.getInt("edit_advanced") = 0;
				advanced = false;

				if (celData.tickLength == 15360)
					celData.selectedTickLength = 0;
				else if (celData.tickLength == 30720)
					celData.selectedTickLength = 1;
				else if (celData.tickLength == 61440)
					celData.selectedTickLength = 2;
				else if (celData.tickLength == 122880)
					celData.selectedTickLength = 3;
				else {
					celData.selectedTickLength = 2;
					celData.tickLength = 61440;
				}
				celData.tickLengthAdvanced = false;
				std::get<hmx_fusion_nodes*>(fusion.nodes.getNode("presets").children[0].value)->getString("layer_select_mode") = "layers";
			}
			ImGui::SameLine();
			if (ImGui::Button("No", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndChild();
		}
		else {
			ImGui::BeginChild("Text", ImVec2(420, 85));
			ImGui::Text("Switch to Advanced Mode?");
			ImGui::EndChild();
			ImGui::BeginChild("Buttons", ImVec2(420, 25));
			if (ImGui::Button("Yes", ImVec2(120, 0)))
			{
				unsavedChanges = true;
				ImGui::CloseCurrentPopup();
				fusion.nodes.getInt("edit_advanced") = 1;
				advanced = true;
				currentAudioFile = 0;
				currentKeyzone = 0;
			}
			ImGui::SameLine();
			if (ImGui::Button("No", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndChild();
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}


	if (advanced) {
		bool addAudio = false;
		bool removeAudio = false;
		ImGui::BeginChild("Audio", ImVec2(aRegion.x, aRegion.y / 3));
		if (currentAudioFile >= moggFiles.size())
			currentAudioFile = 0;
		ImGui::BeginChild("AudioTableWithButtons", ImVec2(aRegion.x / 3, (aRegion.y / 3)));
		ImGui::BeginChild("AudioTableHolder", ImVec2(aRegion.x / 3, (aRegion.y / 3) - 50));
		if (ImGui::BeginTable("AudioTable", 2, 0, ImVec2(aRegion.x / 3, (aRegion.y / 3) - 50))) {
			ImGui::TableSetupColumn("Index", 0, 0.2);
			ImGui::TableSetupColumn("Audio File");
			ImGui::TableHeadersRow();
			for (int i = 0; i < moggFiles.size(); i++)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Selectable((std::to_string(i)).c_str(), currentAudioFile == i, ImGuiSelectableFlags_SpanAllColumns)) {
					currentAudioFile = i;
				}
				ImGui::TableNextColumn(); 
				if (audiolabels.getChild(moggName(moggFiles[i]->fileName)) == nullptr) {
					hmx_fusion_node label;
					label.key = moggName(moggFiles[i]->fileName);
					label.value = moggName(moggFiles[i]->fileName);
					audiolabels.children.push_back(label);
				}
				ImGui::Text(audiolabels.getString(moggName(moggFiles[i]->fileName)).c_str());

			}
			ImGui::EndTable();
		}
		ImGui::EndChild();
		if (ImGui::Button("Add Audio File")) {
			unsavedChanges = true;
			addAudio = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove Audio File") && moggFiles.size() != 1) {
			unsavedChanges = true;
			removeAudio = true;
		}
		ImGui::EndChild();
		ImGui::SameLine();

		ImGui::BeginChild("AudioSettings", ImVec2((aRegion.x / 3) * 2, (aRegion.y / 3)));
		
		if(ImGui::InputText("Audio File Label", &audiolabels.getString(moggName(moggFiles[currentAudioFile]->fileName))))
			unsavedChanges = true;
		ImGui::Checkbox("Replace label on .ogg load", &replaceAudioLabel);
		display_mogg_settings(fusionFile, currentAudioFile, *moggFiles[currentAudioFile], "");
		ImGui::EndChild();

		ImGui::EndChild();

		ImGui::BeginChild("Keymap", ImVec2(aRegion.x, ImGui::GetContentRegionAvail().y - 32));



		if (currentKeyzone >= map.children.size())
			currentKeyzone = 0;
		ImGui::BeginChild("KeyzoneTableAndButtons", ImVec2(aRegion.x / 3, (aRegion.y / 2)));
		ImGui::BeginChild("KeyzoneTableHolder", ImVec2(aRegion.x / 3, (aRegion.y / 2) - 50));
		if (ImGui::BeginTable("KeyzoneTable", 2, 0, ImVec2(aRegion.x / 3, (aRegion.y / 2) - 50))) {
			ImGui::TableSetupColumn("Index", 0, 0.2);
			ImGui::TableSetupColumn("Keyzone Label");
			ImGui::TableHeadersRow();
			for (int i = 0; i < map.children.size(); i++)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Selectable((std::to_string(i)).c_str(), currentKeyzone == i, ImGuiSelectableFlags_SpanAllColumns)) {
					currentKeyzone = i;
				}
				ImGui::TableNextColumn();
				ImGui::Text(std::get<hmx_fusion_nodes*>(map.children[i].value)->getString("zone_label").c_str());
			}
			ImGui::EndTable();
		}



		ImGui::EndChild();
		if (ImGui::Button("Add Keyzone")) {
			unsavedChanges = true;
			map.children.emplace_back(map.children[currentKeyzone]);
			std::string str = hmx_fusion_parser::outputData(map);
			std::vector<std::uint8_t> vec(str.begin(), str.end());
			map = hmx_fusion_parser::parseData(vec);
			std::get<hmx_fusion_nodes*>(map.children[map.children.size() - 1].value)->getString("zone_label") = "New Zone";
			currentKeyzone = map.children.size() - 1;
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove Keyzone") && map.children.size() != 1) {
			unsavedChanges = true;
			int mapToErase = currentKeyzone;
			if (currentKeyzone == map.children.size() - 1)
				currentKeyzone--;
			map.children.erase(map.children.begin() + mapToErase);
		}
		ImGui::EndChild();
		ImVec2 cursorPos = ImGui::GetCursorPos();
		cursorPos.y -= 10;
		ImGui::SameLine();

		ImGui::BeginChild("KeymapSettings", ImVec2((aRegion.x / 3) * 2, ImGui::GetContentRegionAvail().y));
		display_keyzone_settings(std::get<hmx_fusion_nodes*>(map.children[currentKeyzone].value), moggFiles, &audiolabels, &map);
		ImGui::EndChild();

		ImGui::EndChild();

		if (addAudio) {
			hmx_fusion_node label;
			HmxAudio::PackageFile newFile = *moggFiles[0];
			int i = 0;
			bool unique = false;
			while (!unique) {
				std::unordered_set<std::string> fileNames;
				for (auto mogg : moggFiles) {
					fileNames.insert(mogg->fileName);
				}
				if (fileNames.count("C:/" + celShortName + "_" + std::to_string(i) + ".mogg") > 0)
					i++;
				else
					unique = true;
			}
			label.key = celShortName + "_" + std::to_string(i);
			label.value = celShortName + "_" + std::to_string(i);
			audiolabels.children.emplace_back(label);
			newFile.fileName = "C:/" + celShortName + "_" + std::to_string(i) + ".mogg";
			asset.audio.audioFiles.insert(asset.audio.audioFiles.begin() + i, newFile);
			moggFiles.clear();
			for (auto&& file : asset.audio.audioFiles) {
				if (file.fileType == "FusionPatchResource") {
					fusionPackageFile = &file;
				}
				else if (file.fileType == "MoggSampleResource") {
					moggFiles.emplace_back(&file);
				}
			}
			currentAudioFile = i;
		}
		if (removeAudio) {
			auto selectedRemove = moggFiles[currentAudioFile];
			audiolabels.children.erase(audiolabels.children.begin() + currentAudioFile);

			if (currentAudioFile == moggFiles.size() - 1)
				currentAudioFile--;
			auto it = std::find_if(
				asset.audio.audioFiles.begin(), asset.audio.audioFiles.end(),
				[&selectedRemove](const HmxAudio::PackageFile& p) {
					return &p == selectedRemove;
				}
			);
			int audioIndex = std::distance(asset.audio.audioFiles.begin(), it);
			asset.audio.audioFiles.erase(it);
			fusionFile.playableMoggs.erase(fusionFile.playableMoggs.begin() + audioIndex);
			moggFiles.clear();
			for (auto&& file : asset.audio.audioFiles) {
				if (file.fileType == "FusionPatchResource") {
					fusionPackageFile = &file;
				}
				else if (file.fileType == "MoggSampleResource") {
					moggFiles.emplace_back(&file);
				}
			}
			int i = 0;
			std::vector<std::string> fileNames;
			for (auto& mogg : moggFiles) {
				fileNames.emplace_back(mogg->fileName);
				mogg->fileName = "C:/" + celShortName + "_" + std::to_string(i) + ".mogg";
				audiolabels.children[i].key = celShortName + "_" + std::to_string(i);
				i++;
			}
			for (auto c : map.children) {
				auto&& nodes = std::get<hmx_fusion_nodes*>(c.value);
				for (int j = 0; j < fileNames.size(); j++)
				{
					if (nodes->getString("sample_path") == fileNames[j]) {
						nodes->getString("sample_path") = moggFiles[j]->fileName;
					}
					else {
						nodes->getString("sample_path") = moggFiles[0]->fileName;
					}
				}
			}
		}
	}
	else {
		ImGui::BeginChild("SimpleUI", ImVec2((aRegion.x / 3) * 2, ImGui::GetContentRegionAvail().y - 32));
		bool duplicate_changed = false;
		if (duplicate_moggs) {
			ImGui::Text("Duplicated");
			display_mogg_settings(fusionFile, 0, *moggFiles[0], "Duplicated");
		}
		else {
			ImGui::Text("Major");
			display_mogg_settings(fusionFile, 0, *moggFiles[0], "Major");
		}


		duplicate_changed = ImGui::Checkbox("Duplicate Audio?", &duplicate_moggs);

		if (duplicate_changed) {
			unsavedChanges = true;
			if (duplicate_moggs) {
				if (moggFiles.size() == 2) {
					asset.audio.audioFiles.erase(asset.audio.audioFiles.begin() + 1);

					moggFiles.clear();
					for (auto&& file : asset.audio.audioFiles) {
						if (file.fileType == "FusionPatchResource") {
							fusionPackageFile = &file;
						}
						else if (file.fileType == "MoggSampleResource") {
							moggFiles.emplace_back(&file);
						}
					}
				}

				auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
				auto map = fusion.nodes.getNode("keymap");

				if (map.children.size() == 2) {
					std::get<hmx_fusion_nodes*>(map.children[1].value)->getString("sample_path") = std::get<hmx_fusion_nodes*>(map.children[0].value)->getString("sample_path");
				}
			}
			else {
				if (moggFiles.size() == 1) {
					auto newFile = *moggFiles[0];
					newFile.fileName = "C:/" + celShortName + "_1.mogg";
					asset.audio.audioFiles.insert(asset.audio.audioFiles.begin() + 1, newFile);

					moggFiles.clear();
					for (auto&& file : asset.audio.audioFiles) {
						if (file.fileType == "FusionPatchResource") {
							fusionPackageFile = &file;
						}
						else if (file.fileType == "MoggSampleResource") {
							moggFiles.emplace_back(&file);
						}
					}
				}

				auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
				auto map = fusion.nodes.getNode("keymap");

				if (map.children.size() == 2) {
					std::get<hmx_fusion_nodes*>(map.children[1].value)->getString("sample_path") = "C:/" + celShortName + "_1.mogg";
				}
			}
		}
		ImGui::Spacing();

		if (!duplicate_moggs) {
			ImGui::Text("Minor");
			display_mogg_settings(fusionFile, 1, *moggFiles[1], "Minor");
		}
		ImGui::Spacing();

		std::vector<hmx_fusion_nodes*>nodes;
		nodes.emplace_back(std::get<hmx_fusion_nodes*>(map.children[0].value));
		nodes.emplace_back(std::get<hmx_fusion_nodes*>(map.children[1].value));
		bool unp = nodes[0]->getInt("unpitched") == 1;
		bool unp_changed = ImGui::Checkbox("Unpitched", &unp);
		if (unp_changed) {
			unsavedChanges = true;
			if (unp) {
				nodes[0]->getInt("unpitched") = 1;
				nodes[1]->getInt("unpitched") = 1;
			}
			else {
				nodes[0]->getInt("unpitched") = 0;
				nodes[1]->getInt("unpitched") = 0;
			}
		}

		if (isRiser) {
			nodes[0]->getInt("singleton") = 0;
			nodes[1]->getInt("singleton") = 0;
		}
		else {
			nodes[0]->getInt("singleton") = 1;
			nodes[1]->getInt("singleton") = 1;
		}

		auto&& ts = nodes[0]->getNode("timestretch_settings");
		auto&& ts2 = nodes[1]->getNode("timestretch_settings");
		bool natp = ts.getInt("maintain_formant") == 1;
		bool natp_changed = ImGui::Checkbox("Natural Pitching", &natp);
		if (natp_changed) {
			unsavedChanges = true;
			if (natp) {
				ts.getInt("maintain_formant") = 1;
				ts2.getInt("maintain_formant") = 1;
			}
			else {
				ts.getInt("maintain_formant") = 0;
				ts2.getInt("maintain_formant") = 0;
			}
		}
		if (ts.getChild("orig_tempo_sync") == nullptr) {
			hmx_fusion_node label;
			label.key = "orig_tempo_sync";
			label.value = 1;
			ts.children.insert(ts.children.begin(), label);
		}

		bool orig_tempo_sync = ts.getInt("orig_tempo_sync") == 1;
		bool ots_changed = ImGui::Checkbox("Sync orig_tempo to song tempo", &orig_tempo_sync);
		ImGui::SameLine();
		HelpMarker("If unchecked, will allow changing orig_tempo to a different value than the song's bpm, and the game will timestretch accordingly");
		if (ots_changed) {
			unsavedChanges = true;
			if (orig_tempo_sync) {
				ts.getInt("orig_tempo_sync") = 1;
			}
			else {
				ts.getInt("orig_tempo_sync") = 0;
			}
		}
		if (!orig_tempo_sync) {
			if (ImGui::InputScalar("Original Tempo", ImGuiDataType_U32, &ts.getInt("orig_tempo"))) {
				ts2.getInt("orig_tempo") = ts.getInt("orig_tempo");
				unsavedChanges = true;
			}
		}

		ImGui::EndChild();
	}

	if (ImGui::Button(advBtn.c_str()))
		ImGui::OpenPopup("Switch Modes?");
}

int chordsTab = 0;
int lastChordsTab = 0;
bool copiedChordsMinor = false;
std::vector<HmxAudio::PackageFile::MidiFileResource::Chord> chordCopyBuffer;
std::vector<HmxAudio::PackageFile::MidiFileResource::Chord> chordCopyBufferOppositeMode;

static bool compareChords(HmxAudio::PackageFile::MidiFileResource::Chord& chord1, HmxAudio::PackageFile::MidiFileResource::Chord& chord2) {
	return chord1.start < chord2.start;
}

static void display_chord_edit(CelData& celData, ImVec2& windowSize, float oggWindowSize, bool minor = false)
{
	if (minor)
		chordsTab = 1;
	else
		chordsTab = 0;
	if (lastChordsTab != chordsTab)
		curChord = -1;
	lastChordsTab = chordsTab;
	AssetLink<MidiSongAsset>* midiSong = nullptr;
	midiSong = minor ? &celData.minorAssets[0] : &celData.majorAssets[0];
	auto&& midi_file = midiSong->data.midiFile.data;
	auto&& midiAsset = std::get<HmxAssetFile>(midi_file.file.e->getData().data.catagoryValues[0].value);
	auto& mfr = std::get<HmxAudio::PackageFile::MidiFileResource>(midiAsset.audio.audioFiles[0].resourceHeader);

	ImGui::BeginChild("ChordTableHolder", ImVec2((windowSize.x / 3) - 15, oggWindowSize - 240));
	if (ImGui::BeginTable("ChordTable", 2, 0, ImVec2((windowSize.x / 3) - 15, oggWindowSize - 240))) {
		ImGui::TableSetupColumn("Beat", 0, 0.2);
		ImGui::TableSetupColumn("Chord");
		ImGui::TableHeadersRow();

		if (mfr.chords.size() > 0) {
			for (int i = 0; i < mfr.chords.size(); i++)
			{

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				
				if (ImGui::Selectable(formatFloatString(std::to_string(mfr.chords[i].start / 480.0F),2).c_str(), curChord == i)) {
					curChord = i;
					chordInput = mfr.chords[i].start / 480.0F;
				}
				ImGui::TableNextColumn();

				// Use ImGui::Combo for the dropdown with chord names
				int selectedChordIndex = -1;
				if (fcsc_cfg.oppositeChordsAfterCurMode) {
					if (minor) {
						for (int j = 0; j < IM_ARRAYSIZE(chordNamesMinorMajor); j++) {
							if (mfr.chords[i].name == chordNamesMinorMajor[j]) {
								selectedChordIndex = j;
								break;
							}
						}
					}
					else {
						for (int j = 0; j < IM_ARRAYSIZE(chordNamesMajorMinor); j++) {
							if (mfr.chords[i].name == chordNamesMajorMinor[j]) {
								selectedChordIndex = j;
								break;
							}
						}
					}
				}
				else {
					for (int j = 0; j < IM_ARRAYSIZE(chordNamesInterleaved); j++) {
						if (mfr.chords[i].name == chordNamesInterleaved[j]) {
							selectedChordIndex = j;
							break;
						}
					}
				}

				
				
				if (selectedChordIndex == -1) {
					selectedChordIndex = 0; // Default to the first chord if not found
				}
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2)); // Adjust padding as needed
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0)); // No spacing
				if (fcsc_cfg.oppositeChordsAfterCurMode) {
					if (minor) {
						if (ImGui::BeginCombo(("##ChordCombo" + std::to_string(i)).c_str(), chordNamesMinorMajor[selectedChordIndex])) {
							for (int k = 0; k < IM_ARRAYSIZE(chordNamesMinorMajor); ++k)
							{
								
								bool is_selected = (selectedChordIndex == k);
								//used to work checking if the value at the current index is "sep" but it stopped working for some reason?????? so hardcoding the values
								if (k==7 || k==14) {
									float separatorPadding = 2.0f; // Adjust the padding value as needed
									float originalCursorPosY = ImGui::GetCursorPosY();

									ImGui::SetCursorPosY(originalCursorPosY + separatorPadding);
									ImGui::Separator();

									ImGui::SetCursorPosY(originalCursorPosY + separatorPadding + separatorPadding);
								}
								else {
									if (ImGui::Selectable(chordNamesMinorMajor[k], is_selected))
									{
										selectedChordIndex = k;
										mfr.chords[i].name = chordNamesMinorMajor[selectedChordIndex];
									}
									if (is_selected)
									{
										ImGui::SetItemDefaultFocus();
									}
								}
							}
							ImGui::EndCombo();
						}
					}
					else {
						if (ImGui::BeginCombo(("##ChordCombo" + std::to_string(i)).c_str(), chordNamesMajorMinor[selectedChordIndex])) {
							for (int k = 0; k < IM_ARRAYSIZE(chordNamesMajorMinor); ++k)
							{

								bool is_selected = (selectedChordIndex == k);
								if (k==6 || k==14) {
									float separatorPadding = 2.0f; // Adjust the padding value as needed
									float originalCursorPosY = ImGui::GetCursorPosY();

									ImGui::SetCursorPosY(originalCursorPosY + separatorPadding);
									ImGui::Separator();

									ImGui::SetCursorPosY(originalCursorPosY + separatorPadding + separatorPadding);
								}
								else {
									if (ImGui::Selectable(chordNamesMajorMinor[k], is_selected))
									{
										selectedChordIndex = k;
										mfr.chords[i].name = chordNamesMajorMinor[selectedChordIndex];
									}
									if (is_selected)
									{
										ImGui::SetItemDefaultFocus();
									}
								}
							}
							ImGui::EndCombo();
						}
					}
				}
				else {
					if (ImGui::Combo(("##ChordCombo" + std::to_string(i)).c_str(), &selectedChordIndex, chordNamesInterleaved, IM_ARRAYSIZE(chordNamesInterleaved))) {
						unsavedChanges = true;
						mfr.chords[i].name = chordNamesInterleaved[selectedChordIndex];
					}
				}
				
				ImGui::PopStyleVar(2);
			
			}
		}

		ImGui::EndTable();
	}
	ImGui::EndChild();


	ImGui::BeginChild("ChordsButtons", ImVec2(windowSize.x / 3, 145));
	if (ImGui::InputFloat("Chord Beat", &chordInput, 0.0F, 0.0F, "%.2f", ImGuiInputTextFlags_CharsDecimal)) {
		chordInput = std::round(std::clamp(chordInput, 0.0F, celData.tickLength / 480.0F) * 100) / 100;
		chordInputTicks = chordInput * 480;
	}
	if (ImGui::Button("Add Chord")) {
		unsavedChanges = true;
		chordInput = std::round(std::clamp(chordInput, 0.0F, celData.tickLength / 480.0F) * 100) / 100;
		chordInputTicks = chordInput * 480;
		if (mfr.chords.size() > 0) {
			bool chordExists = false;
			for (int i = 0; i < mfr.chords.size(); i++) {
				if (mfr.chords[i].start == chordInputTicks) {
					chordExists = true;
					break;
				}
			}
			if (!chordExists) {
				HmxAudio::PackageFile::MidiFileResource::Chord newChord;
				newChord.start = chordInputTicks;
				newChord.end = newChord.start + 1;
				newChord.name = minor ? "1m" : "1";
				mfr.chords.emplace_back(newChord);
				std::sort(mfr.chords.begin(), mfr.chords.end(), compareChords);
				for (int i = 0; i < mfr.chords.size(); i++) {
					if (mfr.chords[i].start == chordInputTicks) {
						curChord = i;
						break;
					}
				}
			}
		}
		else {
			HmxAudio::PackageFile::MidiFileResource::Chord newChord;
			newChord.start = chordInputTicks;
			newChord.end = mfr.final_tick;
			newChord.name = minor ? "1m" : "1";
			mfr.chords.emplace_back(newChord);
			curChord = 0;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Update Chord Beat") && mfr.chords.size() > 0) {
		unsavedChanges = true;
		chordInput = std::round(std::clamp(chordInput, 0.0F, celData.tickLength / 480.0F) * 100) / 100;
		chordInputTicks = chordInput * 480;
		if (mfr.chords.size() == 1) {
			mfr.chords[curChord].start = chordInputTicks;
		}
		else {
			bool chordExists = false;
			for (int i = 0; i < mfr.chords.size(); i++) {
				if (mfr.chords[i].start == chordInputTicks) {
					chordExists = true;
					break;
				}
			}
			if (!chordExists) {
				mfr.chords[curChord].start = chordInputTicks;
				std::sort(mfr.chords.begin(), mfr.chords.end(), compareChords);
				for (int i = 0; i < mfr.chords.size(); i++) {
					if (mfr.chords[i].start == chordInputTicks) {
						curChord = i;
						break;
					}
				}
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Remove Chord") && mfr.chords.size() > 0) {
		unsavedChanges = true;
		int chordToErase = curChord;
		if (curChord == mfr.chords.size() - 1) {
			curChord--;
		}
		mfr.chords.erase(mfr.chords.begin() + chordToErase);
		if (mfr.chords.size() > 0) {
			chordInput = mfr.chords[curChord].start / 480.0F;
		}
	}
	ImGui::NewLine();
	ImGui::NewLine();
	if (ImGui::Button("Copy Chords")) {
		chordCopyBuffer = mfr.chords;
		chordCopyBufferOppositeMode = convertChordsMode(mfr.chords,!minor);
		copiedChordsMinor = minor;
	}
	ImGui::SameLine();
	if (ImGui::Button("Paste Chords")) {
		if (chordCopyBuffer.size() > 0) {
			ImGui::OpenPopup("Paste Chords?");
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear Chords")) {
		ImGui::OpenPopup("Clear Chords?");
	}
	if (ImGui::BeginPopupModal("Clear Chords?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::BeginChild("Text", ImVec2(420, 69));
		ImGui::TextWrapped("Are you sure you would like to clear chords on the current tab?");
		ImGui::Text("WARNING: Will erase all chords on the current tab");
		ImGui::EndChild();
		ImGui::BeginChild("Buttons", ImVec2(420, 25));
		if (ImGui::Button("Yes", ImVec2(120, 0)))
		{
			unsavedChanges = true;
			mfr.chords.clear();
			curChord = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("No", ImVec2(120, 0)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndChild();


		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal("Paste Chords?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::BeginChild("Text", ImVec2(420, 69));
		ImGui::TextWrapped("Are you sure you would like to paste chords into the current tab?");
		ImGui::Text("WARNING: Will overwrite all chords on the current tab");
		ImGui::EndChild();
		ImGui::BeginChild("Buttons", ImVec2(420, 25));
		if (ImGui::Button("Yes", ImVec2(120, 0)))
		{
			unsavedChanges = true;
			if (copiedChordsMinor == minor)
				mfr.chords = chordCopyBuffer;
			else
				mfr.chords = chordCopyBufferOppositeMode;
			curChord = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("No", ImVec2(120, 0)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndChild();


		ImGui::EndPopup();
	}
	ImGui::EndChild();

}

static void display_cel_data(CelData& celData, FuserEnums::KeyMode::Value currentKeyMode) {
	int disc_midi_maj_single;
	int disc_midi_min_single; 
	{
		AssetLink<MidiSongAsset>* midiSong = &celData.minorAssets[0];
		auto&& midi_file = midiSong->data.midiFile.data;
		auto&& mfr = std::get<HmxAudio::PackageFile::MidiFileResource>(std::get<HmxAssetFile>(midi_file.file.e->getData().data.catagoryValues[0].value).audio.audioFiles[0].resourceHeader);
		mfr.minor = true;
		mfr.is_single_note = mfr.MFR_is_single_note();
		disc_midi_min_single = mfr.is_single_note;
	}
	{
		AssetLink<MidiSongAsset>* midiSong = &celData.majorAssets[0];
		auto&& midi_file = midiSong->data.midiFile.data;
		auto&& mfr = std::get<HmxAudio::PackageFile::MidiFileResource>(std::get<HmxAssetFile>(midi_file.file.e->getData().data.catagoryValues[0].value).audio.audioFiles[0].resourceHeader);
		mfr.is_single_note = mfr.MFR_is_single_note();
		disc_midi_maj_single = mfr.is_single_note;
	}
	ChooseFuserEnum<FuserEnums::Instrument>("Instrument", celData.instrument, false);
	auto&& fusionFile = celData.majorAssets[0].data.fusionFile.data;

	auto&& asset = std::get<HmxAssetFile>(fusionFile.file.e->getData().data.catagoryValues[0].value);
	//auto &&mogg = asset.audio.audioFiles[0];

	HmxAudio::PackageFile* fusionPackageFile = nullptr;
	std::vector<HmxAudio::PackageFile*> moggFiles;
	std::unordered_set<std::string> fusion_mogg_files;


	bool disc_advanced;
	
	{
		for (auto&& file : asset.audio.audioFiles) {
			if (file.fileType == "FusionPatchResource") {
				fusionPackageFile = &file;
			}
			else if (file.fileType == "MoggSampleResource") {
				moggFiles.emplace_back(&file);
			}
		}

		auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
		auto map = fusion.nodes.getNode("keymap");


		if (fusion.nodes.getChild("edit_advanced") == nullptr) {
			hmx_fusion_node label;
			label.key = "edit_advanced";
			label.value = 0;
			fusion.nodes.children.insert(fusion.nodes.children.begin(), label);
			disc_advanced = false;
		}
		else {
			disc_advanced = fusion.nodes.getInt("edit_advanced") == 1;
		}

		int mapidx = 0;
		for (auto c : map.children) {
			auto nodes = std::get<hmx_fusion_nodes*>(c.value);
			fusion_mogg_files.emplace(nodes->getString("sample_path"));
			if (nodes->getChild("zone_label") == nullptr) {
				hmx_fusion_node label;
				label.key = "zone_label";
				if (!disc_advanced) {
					if (mapidx == 0) {
						label.value = "Major";
					}
					else if (mapidx == 1) {
						label.value = "Minor";
					}
					else {
						label.value = "UNKNOWN";
					}
				}
				else { label.value = "Keyzone " + std::to_string(mapidx); }
				nodes->children.insert(nodes->children.begin(), label);
			}
			if (nodes->getChild("keymap_preset") == nullptr) {
				hmx_fusion_node kmpreset;
				kmpreset.key = "keymap_preset";
				int nmin = nodes->getInt("min_note");
				int nmax = nodes->getInt("max_note");
				int nroot = nodes->getInt("root_note");
				int mivel = nodes->getInt("min_velocity");
				int mavel = nodes->getInt("max_velocity");
				int so = nodes->getInt("start_offset_frame");
				int eo = nodes->getInt("end_offset_frame");
				if (mivel != 0 || mavel != 127 || so != -1 || eo != -1)
					kmpreset.value = 3;
				else {
					if (nmin == 0) {
						if (nroot == 60) {
							if (nmax == 127)
								kmpreset.value = 2;
							else if (nmax == 71)
								kmpreset.value = 0;
							else
								kmpreset.value = 3;
						}
						else
							kmpreset.value = 3;
					}
					else if (nmin == 72) {
						if (nroot == 84) {
							if (nmax == 127)
								kmpreset.value = 1;
							else
								kmpreset.value = 3;
						}
						else
							kmpreset.value = 3;
					}
					else
						kmpreset.value = 3;
				}




				nodes->children.insert(nodes->children.begin(), kmpreset);
			}
			mapidx++;
		}
	}

	bool duplicate_moggs = fusion_mogg_files.size() == 1;

	// Riser Moggs

	auto&& fusionFileRiser = celData.songTransitionFile.data.majorAssets[0].data.fusionFile.data;
	auto&& assetRiser = std::get<HmxAssetFile>(fusionFileRiser.file.e->getData().data.catagoryValues[0].value);
	//auto &&mogg = asset.audio.audioFiles[0];

	HmxAudio::PackageFile* fusionPackageFileRiser = nullptr;
	std::vector<HmxAudio::PackageFile*> moggFilesRiser;
	std::unordered_set<std::string> fusion_mogg_filesRiser;

	bool rise_advanced;
	{
		for (auto&& file : assetRiser.audio.audioFiles) {
			if (file.fileType == "FusionPatchResource") {
				fusionPackageFileRiser = &file;
			}
			else if (file.fileType == "MoggSampleResource") {
				moggFilesRiser.emplace_back(&file);
			}
		}

		auto&& fusionRiser = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFileRiser->resourceHeader);
		auto mapRiser = fusionRiser.nodes.getNode("keymap");


		if (fusionRiser.nodes.getChild("edit_advanced") == nullptr) {
			hmx_fusion_node label;
			label.key = "edit_advanced";
			label.value = 0;
			fusionRiser.nodes.children.insert(fusionRiser.nodes.children.begin(), label);
			rise_advanced = false;
		}
		else {
			rise_advanced = fusionRiser.nodes.getInt("edit_advanced") == 1;
		}

		int mapidx = 0;
		for (auto c : mapRiser.children) {
			auto nodesRiser = std::get<hmx_fusion_nodes*>(c.value);
			fusion_mogg_filesRiser.emplace(nodesRiser->getString("sample_path"));
			if (nodesRiser->getChild("zone_label") == nullptr) {
				hmx_fusion_node label;
				label.key = "zone_label";
				if (mapidx == 0) {
					label.value = "Major";
				}
				else if (mapidx == 1) {
					label.value = "Minor";
				}
				else {
					label.value = "UNKNOWN";
				}
				nodesRiser->children.insert(nodesRiser->children.begin(), label);
			}
			if (nodesRiser->getChild("keymap_preset") == nullptr) {
				hmx_fusion_node kmpreset;
				kmpreset.key = "keymap_preset";
				int nmin = nodesRiser->getInt("min_note");
				int nmax = nodesRiser->getInt("max_note");
				int nroot = nodesRiser->getInt("root_note");
				int mivel = nodesRiser->getInt("min_velocity");
				int mavel = nodesRiser->getInt("max_velocity");
				int so = nodesRiser->getInt("start_offset_frame");
				int eo = nodesRiser->getInt("end_offset_frame");
				if (mivel != 0 || mavel != 127 || so != -1 || eo != -1)
					kmpreset.value = 3;
				else {
					if (nmin == 0) {
						if (nroot == 60) {
							if (nmax == 127)
								kmpreset.value = 2;
							else if (nmax == 71)
								kmpreset.value = 0;
							else
								kmpreset.value = 3;
						}
						else
							kmpreset.value = 3;
					}
					else if (nmin == 72) {
						if (nroot == 84) {
							if (nmax == 127)
								kmpreset.value = 1;
							else
								kmpreset.value = 3;
						}
						else
							kmpreset.value = 3;
					}
					else
						kmpreset.value = 3;
				}

				nodesRiser->children.insert(nodesRiser->children.begin(), kmpreset);
			}
			mapidx++;
		}
	}

	bool duplicate_moggsRiser = fusion_mogg_filesRiser.size() == 1;

	ImGui::NewLine();


	auto windowSize = ImGui::GetWindowSize();

	auto oggWindowSize = ImGui::GetContentRegionAvail().y - 70;
	if (ImGui::BeginTabBar("CelDataEditTabs")) {
		if (ImGui::BeginTabItem("Disc Audio")) {
			ImGui::BeginChild("AudioSettingsDisc", ImVec2((windowSize.x / 3) * 2, oggWindowSize), true);
			display_cel_audio_options(celData, asset, moggFiles, fusionFile, fusionPackageFile, duplicate_moggs, false);
			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2, 0.2, 0.2, 1));
			ImGui::BeginChild("Advanced - Disc", ImVec2(ImGui::GetContentRegionAvail().x, oggWindowSize), true);
			if (ImGui::BeginTabBar("DiscAdvancedTabs")) {
				if (ImGui::BeginTabItem("Fusion/MIDI")) {
					display_fusionmidisettings(asset, celData, fusionPackageFile, moggFiles, disc_advanced, disc_midi_maj_single, disc_midi_min_single);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Pickups")) {
					ImGui::BeginChild("PickupTableHolder", ImVec2((windowSize.x / 3) - 15, oggWindowSize - 240));
					if (ImGui::BeginTable("PickupTable", 2, 0, ImVec2((windowSize.x / 3) - 15, oggWindowSize - 240))) {
						ImGui::TableSetupColumn("Index", 0, 0.2);
						ImGui::TableSetupColumn("Pickup Beat");
						ImGui::TableHeadersRow();
						if (celData.pickupArray->values.size() > 0) {
							for (int i = 0; i < celData.pickupArray->values.size(); i++)
							{
								ImGui::TableNextRow();
								ImGui::TableNextColumn();
								if (ImGui::Selectable((std::to_string(i)).c_str(), curPickup == i, ImGuiSelectableFlags_SpanAllColumns)) {
									curPickup = i;
									pickupInput = std::get<PrimitiveProperty<float>>(celData.pickupArray->values[i]->v).data;
								}
								ImGui::TableNextColumn();
								std::string pickupPos = std::to_string(std::get<PrimitiveProperty<float>>(celData.pickupArray->values[i]->v).data);
								pickupPos = pickupPos.substr(0, pickupPos.find(".") + 3);
								ImGui::Text(pickupPos.c_str());
							}
						}

						ImGui::EndTable();
					}
					ImGui::EndChild();
					ImGui::BeginChild("PickupButtons", ImVec2(windowSize.x / 3, 145));
					if (ImGui::InputFloat("Pickup Beat", &pickupInput, 0.0F, 0.0F, "%.2f", ImGuiInputTextFlags_CharsDecimal)) {
						pickupInput = std::round(std::clamp(pickupInput, 0.0F, 128.0F) * 100) / 100;
					}
					if (ImGui::Button("Add Pickup")) {
						unsavedChanges = true;
						pickupInput = std::round(std::clamp(pickupInput, 0.0F, 128.0F) * 100) / 100;

						if (celData.pickupArray->values.size() > 0) {
							std::vector<float> pickups;

							for (auto puv : celData.pickupArray->values) {
								pickups.emplace_back(std::get<PrimitiveProperty<float>>(puv->v).data);
							}

							auto it = std::find(pickups.begin(), pickups.end(), pickupInput);

							if (it == pickups.end()) {
								pickups.emplace_back(pickupInput);
								std::sort(pickups.begin(), pickups.end());
								auto last = std::unique(pickups.begin(), pickups.end());
								pickups.erase(last, pickups.end());
								celData.pickupArray->values.clear();
								for (int i = 0; i < pickups.size(); i++) {
									celData.pickupArray->values.emplace_back(new IPropertyValue);
									celData.pickupArray->values[i]->v = asset_helper::createPropertyValue("FloatProperty");
									std::get<PrimitiveProperty<float>>(celData.pickupArray->values[i]->v).data = pickups[i];
								}
								auto it2 = std::find(pickups.begin(), pickups.end(), pickupInput);
								if (it2 != pickups.end()) {
									curPickup = std::distance(pickups.begin(), it2);
								}
							}
						}
						else {
							celData.pickupArray->values.emplace_back(new IPropertyValue);
							celData.pickupArray->values[0]->v = asset_helper::createPropertyValue("FloatProperty");
							std::get<PrimitiveProperty<float>>(celData.pickupArray->values[0]->v).data = pickupInput;
							curPickup = 0;
						}


					}
					ImGui::SameLine();
					if (ImGui::Button("Update Pickup") && celData.pickupArray->values.size() > 0) {
						unsavedChanges = true;
						pickupInput = std::round(std::clamp(pickupInput, 0.0F, 128.0F) * 100) / 100;
						if (celData.pickupArray->values.size() == 1) {
							std::get<PrimitiveProperty<float>>(celData.pickupArray->values[curPickup]->v).data = pickupInput;
						}
						else {
							std::vector<float> pickups;
							for (auto puv : celData.pickupArray->values) {
								pickups.emplace_back(std::get<PrimitiveProperty<float>>(puv->v).data);
							}

							auto it = std::find(pickups.begin(), pickups.end(), pickupInput);
							if (it == pickups.end()) {
								pickups[curPickup] = pickupInput;
								std::sort(pickups.begin(), pickups.end());
								auto last = std::unique(pickups.begin(), pickups.end());
								pickups.erase(last, pickups.end());
								celData.pickupArray->values.clear();
								for (int i = 0; i < pickups.size(); i++) {
									celData.pickupArray->values.emplace_back(new IPropertyValue);
									celData.pickupArray->values[i]->v = asset_helper::createPropertyValue("FloatProperty");
									std::get<PrimitiveProperty<float>>(celData.pickupArray->values[i]->v).data = pickups[i];
								}
								auto it2 = std::find(pickups.begin(), pickups.end(), pickupInput);
								if (it2 != pickups.end()) {
									curPickup = std::distance(pickups.begin(), it2);
								}
							}
						}

					}
					ImGui::SameLine();
					if (ImGui::Button("Remove Pickup") && celData.pickupArray->values.size() > 0) {
						unsavedChanges = true;
						int pickupToErase = curPickup;
						if (curPickup == celData.pickupArray->values.size() - 1) {
							curPickup--;
						}
						celData.pickupArray->values.erase(celData.pickupArray->values.begin() + pickupToErase);
						if (celData.pickupArray->values.size() > 0) {
							pickupInput = std::get<PrimitiveProperty<float>>(celData.pickupArray->values[curPickup]->v).data;
						}

					}
					ImGui::NewLine();
					ImGui::NewLine();
					if (ImGui::Button("Clear Pickups")) {
						ImGui::OpenPopup("Clear Pickups?");
					}


					if (ImGui::BeginPopupModal("Clear Pickups?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
					{
						ImGui::PopStyleColor();
						ImGui::BeginChild("Text", ImVec2(420, 69));
						ImGui::Text("Are you sure you would like to clear pickups?");
						ImGui::TextWrapped("WARNING: Will erase all pickups");
						ImGui::EndChild();
						ImGui::BeginChild("Buttons", ImVec2(420, 25));
						if (ImGui::Button("Yes", ImVec2(120, 0)))
						{
							unsavedChanges = true;
							celData.pickupArray->values.clear();
							curPickup = -1;
							ImGui::CloseCurrentPopup();
						}
						ImGui::SameLine();
						if (ImGui::Button("No", ImVec2(120, 0)))
						{
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndChild();


						ImGui::EndPopup();
						ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2, 0.2, 0.2, 1));
					}

					ImGui::EndChild();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Maj Chords")) {
					display_chord_edit(celData, windowSize, oggWindowSize);
					ImGui::EndTabItem();
				}if (ImGui::BeginTabItem("Min Chords")) {
					display_chord_edit(celData, windowSize, oggWindowSize, true);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}


			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Riser Audio")) {
			ImGui::BeginChild("AudioSettingsRiser", ImVec2((windowSize.x / 3) * 2, oggWindowSize), true);
			display_cel_audio_options(celData, assetRiser, moggFilesRiser, fusionFileRiser, fusionPackageFileRiser, duplicate_moggsRiser, true);
			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2, 0.2, 0.2, 1));
			ImGui::BeginChild("Advanced - Riser", ImVec2(ImGui::GetContentRegionAvail().x, oggWindowSize), true);
			display_fusionmidisettings(assetRiser, celData, fusionPackageFileRiser, moggFilesRiser, rise_advanced, 0, 0, true);

			ImGui::PopStyleColor();
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	bool allUnpitched = celData.allUnpitched == true;
	bool allUnpitchedChanged = ImGui::Checkbox("Track has no key?", &allUnpitched);

	if (allUnpitchedChanged) {
		unsavedChanges = true;
		if (allUnpitched) {
			celData.allUnpitched = true;
			celData.songTransitionFile.data.allUnpitched = true;
		}
		else {
			celData.allUnpitched = false;
			celData.songTransitionFile.data.allUnpitched = false;
		}
	}
	ImGui::SameLine();
	HelpMarker("When this is checked, the disc and riser will both have key and mode set to Num, which is no key or mode. This allows for FUSER to change the key/mode when you drop down another disc if this one is playing.");
}

void set_g_pd3dDevice(ID3D11Device* g_pd3dDevice) {
	gCtx.g_pd3dDevice = g_pd3dDevice;
}

std::string windowTitle = " No Song Loaded";
void custom_song_creator_update(size_t width, size_t height) {
	bool do_open = false;
	bool do_open_2 = false;
	bool do_save = false;
	bool do_new = false;
	bool open_preferences = false;

	ImGui::SetNextWindowPos(ImVec2{ 0, 0 });
	ImGui::SetNextWindowSize(ImVec2{ (float)width, (float)height });
	
	ImGuiWindowFlags window_flags = 0;
	window_flags |= ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoResize;
	window_flags |= ImGuiWindowFlags_NoCollapse;
	window_flags |= ImGuiWindowFlags_MenuBar;
	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin((windowTitle+"###FCSC_TITLE").c_str(), nullptr, window_flags); 
	if (filenameArg) {
		std::ifstream infile(filenameArgPath, std::ios_base::binary);
		std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());

		DataBuffer dataBuf;
		dataBuf.setupVector(fileData);
		load_file(std::move(dataBuf));

		gCtx.saveLocation = filenameArgPath;
		filenameArg = false;
	}
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New")) {
				if (gCtx.currentPak == nullptr) {
					load_template(); 
					
				}
				else {
					do_new = true;
				}
			}
			if (ImGui::MenuItem("Open", "Ctrl+O")) {
				do_open = true;
			}
			if (gCtx.currentPak != nullptr) {
				if (ImGui::MenuItem("Save", "Ctrl+S")) {
					do_save = true;
				}
			}
			else {
				ImGui::MenuItem("Save", "Ctrl+S", false, false);
			}


			if (ImGui::MenuItem("Save As..")) {
				select_save_location();
			}
			if (ImGui::MenuItem("Preferences##MENUITEM")) {
				open_preferences = true;
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Audio"))
		{
			std::vector<std::string> devices;

			int a, count = 0;
			BASS_DEVICEINFO info;

			for (a = 0; BASS_GetDeviceInfo(a, &info); a++)
			{
				if (info.flags & BASS_DEVICE_ENABLED) {
					devices.emplace_back(info.name);
				}
			}

			auto get_item = [](void* data, int idx, const char** out) -> bool {
				std::vector<std::string>& devices = *(std::vector<std::string>*)data;
				*out = devices[idx].c_str();

				return true;
				};


			if (ImGui::Combo("Current Device", &gAudio.currentDevice, get_item, &devices, devices.size())) {
				BASS_Stop();
				BASS_SetDevice(gAudio.currentDevice);
				BASS_Start();
			}

			if (ImGui::SliderFloat("Volume", &gAudio.volume, 0, 1)) {
				BASS_SetConfig(
					BASS_CONFIG_GVOL_SAMPLE,
					gAudio.volume * 10000
				);
			}

			ImGui::EndMenu();
		}
#if _DEBUG
		if (ImGui::BeginMenu("Debug Menu"))
		{
			bool extract_uexp = false;
			std::string save_file;
			std::string ext;
			std::function<std::vector<u8>(const Asset&)> getData;

			if (ImGui::MenuItem("Load Pak and pop in album art")) {
				__debugbreak();
			}

			if (ImGui::MenuItem("Extract Midi From uexp")) {
				extract_uexp = true;
				save_file = "Fuser Midi File (*.midi_pc)\0.midi_pc\0";
				ext = "midi_pc";
				getData = [](const Asset& asset) {
					auto&& midiAsset = std::get<HmxAssetFile>(asset.data.catagoryValues[0].value);
					auto&& fileData = midiAsset.audio.audioFiles[0].fileData;
					return fileData;
					};
			}

			if (ImGui::MenuItem("Generate .sig file")) {
				auto file = OpenFile("Unreal Pak File (*.pak)\0*.pak\0");
				if (file) {
					std::string path = *file;
					size_t lastDot = path.find_last_of('.'); 
					std::string sigPath;
					if (lastDot != std::string::npos)
						sigPath = path.substr(0, lastDot) + ".sig";
					else
						sigPath = path + ".sig";
					std::ifstream inFile(path, std::ios::binary);
					// Read the file into the vector
					std::vector<u8> fileData;
					uint8_t byte;
					while (inFile.read(reinterpret_cast<char*>(&byte), sizeof(uint8_t)))
						fileData.push_back(byte);
					DataBuffer outBuf;
					outBuf.setupVector(fileData);
					write_sig(outBuf, sigPath);
				}
				
			}

			if (ImGui::MenuItem("Extract Fusion From uexp")) {
				extract_uexp = true;
				save_file = "Fuser Fusion File (*.fusion)\0.fusion\0";
				ext = "fusion";
				getData = [](const Asset& asset) {
					auto&& assetFile = std::get<HmxAssetFile>(asset.data.catagoryValues[0].value);

					for (auto&& f : assetFile.audio.audioFiles) {
						if (f.fileType == "FusionPatchResource") {
							std::string outStr = hmx_fusion_parser::outputData(std::get<HmxAudio::PackageFile::FusionFileResource>(f.resourceHeader).nodes);
							std::vector<u8> out;
							out.resize(outStr.size());
							memcpy(out.data(), outStr.data(), outStr.size());
							return out;
						}
					}

					return std::vector<u8>();
					};
			}

			if (extract_uexp) {
				auto file = OpenFile("Unreal Asset File (*.uasset)\0*.uasset\0");
				if (file) {
					auto assetFile = fs::path(*file);
					auto uexpFile = assetFile.parent_path() / (assetFile.stem().string() + ".uexp");

					std::ifstream infile(assetFile, std::ios_base::binary);
					std::ifstream uexpfile(uexpFile, std::ios_base::binary);

					std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
					fileData.insert(fileData.end(), std::istreambuf_iterator<char>(uexpfile), std::istreambuf_iterator<char>());


					DataBuffer dataBuf;
					dataBuf.loading = true;
					dataBuf.setupVector(fileData);

					Asset a;
					a.serialize(dataBuf);

					auto out_file = SaveFile(save_file.c_str(), ext.c_str(), "");
					if (out_file) {
						auto fileData = getData(a);
						std::ofstream outfile(*out_file, std::ios_base::binary);
						outfile.write((const char*)fileData.data(), fileData.size());
					}
			}
		}

			ImGui::EndMenu();
	}
#endif

		ImGui::EndMenuBar();
}

	auto&& input = ImGui::GetIO();

	if (input.KeyCtrl) {
		if (input.KeysDown['O'] && input.KeysDownDuration['O'] == 0.0f) {
			do_open = true;
		}
		if (input.KeysDown['S'] && input.KeysDownDuration['S'] == 0.0f) {
			do_save = true;
		}
	}

	if (ImGui::BeginPopupModal("Open without saving?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::BeginChild("PopupHolder", ImVec2(420, 120));
		ImGui::BeginChild("Text", ImVec2(420, 85));
		ImGui::Text("Would you like to open a custom file without saving?");
		ImGui::TextWrapped("WARNING: Any changes you have made will be lost unless you have saved.");
		ImGui::EndChild();
		ImGui::BeginChild("Buttons", ImVec2(420, 25));
		if (ImGui::Button("Yes", ImVec2(120, 0)))
		{
			do_open = true;
			ImGui::CloseCurrentPopup();
			
		}
		ImGui::SameLine();
		if (ImGui::Button("No", ImVec2(120, 0)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndChild();
		ImGui::EndChild();
		ImGui::EndPopup();
	}

	if (do_open) {
		if (unsavedChanges)
			ImGui::OpenPopup("Confirm Open");
		else
			do_open_2 = true;
	}

	if (do_save && gCtx.currentPak != nullptr) {
		if (!gCtx.saveLocation.empty()) {
			save_file();
		}
		else {
			select_save_location();
		}
	}
	if (do_new) {
		if (unsavedChanges)
			ImGui::OpenPopup("Confirm New Custom");
		else {
			load_template();
		}
	}
		

	if (ImGui::BeginPopupModal("Confirm New Custom", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::BeginChild("PopupHolder", ImVec2(420, 120));
		ImGui::BeginChild("Text", ImVec2(420, 85));
		ImGui::Text("Would you like to make a new custom?");
		ImGui::TextWrapped("WARNING: Any changes you have made will be lost unless you have saved.");
		ImGui::EndChild();
		ImGui::BeginChild("Buttons", ImVec2(420, 25));
		if (ImGui::Button("Yes", ImVec2(120, 0)))
		{
			ImGui::CloseCurrentPopup();
			load_template();
		}
		ImGui::SameLine();
		if (ImGui::Button("No", ImVec2(120, 0)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndChild();
		ImGui::EndChild();
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopupModal("Confirm Open", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::BeginChild("PopupHolder", ImVec2(420, 120));
		ImGui::BeginChild("Text", ImVec2(420, 85));
		ImGui::Text("Would you like to open a custom?");
		ImGui::TextWrapped("WARNING: Any changes you have made will be lost unless you have saved.");
		ImGui::EndChild();
		ImGui::BeginChild("Buttons", ImVec2(420, 25));
		if (ImGui::Button("Yes", ImVec2(120, 0)))
		{
			ImGui::CloseCurrentPopup();
			do_open_2 = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("No", ImVec2(120, 0)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndChild();
		ImGui::EndChild();
		ImGui::EndPopup();
	}

	if (do_open_2) {
		auto file = OpenFile("Fuser Custom Song (*.pak)\0*.pak\0");
		if (file) {
			std::ifstream infile(*file, std::ios_base::binary);
			std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());

			DataBuffer dataBuf;
			dataBuf.setupVector(fileData);
			load_file(std::move(dataBuf));

			gCtx.saveLocation = *file;
		}
	}

	if (gCtx.currentPak != nullptr) {
		windowTitle = (unsavedChanges ? "*" : " ") + gCtx.currentPak.get()->root.shortName + ": " + gCtx.currentPak.get()->root.artistName + " - " + gCtx.currentPak.get()->root.songName;
		if (ImGui::BeginTabBar("Tabs")) {
			if (ImGui::BeginTabItem("Main Properties")) {
				ImGui::BeginChild("MainMeta", ImVec2(ImGui::GetContentRegionAvail().x / 2, ImGui::GetContentRegionAvail().y));
				display_main_properties();
				ImGui::EndChild();
				ImGui::SameLine();
				ImGui::BeginChild("MainArt", ImGui::GetContentRegionAvail());
				display_album_art();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			int idx = 0;
			for (auto&& cel : gCtx.currentPak->root.celData) {
				std::string tabName = "Song Cell " + std::to_string(idx / 2) + " - ";
				tabName += cel.data.type.getString();
				tabName += "##Cel" + std::to_string(idx / 2);
				if (ImGui::BeginTabItem(tabName.c_str())) {
					curCelTab = idx;
					display_cel_data(cel.data, gCtx.currentPak->root.keyMode);
					ImGui::EndTabItem();
				}
				idx += 2;
			}

			ImGui::EndTabBar();
		}

		if (Error_InvalidFileName) {
			ImGui::OpenPopup("Invalid File Name");
			Error_InvalidFileName = false;
		}
		auto fileName = gCtx.currentPak->root.shortName + "_P.pak";
		auto error = "Your file must be named as " + fileName + ", otherwise the song loader won't unlock it!";
		ErrorModal("Invalid File Name", error.c_str());
	}
	else {
		ImGui::Text("Welcome to the Fuser Custom Song Creator!");
		ImGui::Text("To get started with the default template, choose File -> New from the menu, or use the button:");
		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
		if (ImGui::Button("Create New Custom Song")) {
			load_template();
		}

		ImGui::Text("To open an existing custom song, use File -> Open.");
	}
	if (closePressed) {
		ImGui::OpenPopup("Exit without saving?");
	}
	if(ImGui::BeginPopupModal("Exit without saving?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::BeginChild("Text", ImVec2(600, 69));
		ImGui::TextWrapped("Exit without saving?");
		ImGui::Text("WARNING: All changes will be lost");
		ImGui::EndChild();
		ImGui::BeginChild("Buttons", ImVec2(600, 25));
		if (ImGui::Button("Save and Exit", ImVec2(200, 0)))
		{
			if (!gCtx.saveLocation.empty()) {
				save_file();
			}
			else {
				select_save_location();
			}
			DestroyWindow(G_hwnd);
		}
		ImGui::SameLine();
		if (ImGui::Button("Exit Without Saving", ImVec2(200, 0)))
		{
			DestroyWindow(G_hwnd);
		}
		ImGui::SameLine();
		if (ImGui::Button("Back", ImVec2(200, 0)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndChild();


		ImGui::EndPopup();
	}
	if(open_preferences)
		ImGui::OpenPopup("Preferences##POPUP");
	
	if (ImGui::BeginPopupModal("Preferences##POPUP", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::BeginChild("Body", ImVec2(600, 250));
		ImGui::Checkbox("Velocity as percentage?", &fcsc_cfg.usePercentVelocity);
		ImGui::SameLine();
		HelpMarker("Some DAWs use 0-100 instead of 0-127 for velocity, check this to use 0-100 for velocity values");
		ImGui::InputText("Default Short Name", &fcsc_cfg.defaultShortName, ImGuiInputTextFlags_CallbackCharFilter, ValidateShortName);
		ImGui::SameLine();
		HelpMarker("The default short name for customs, useful to add a prefix automatically. Will reset to the default of 'custom_song' if left blank");
		ImGui::Checkbox("Opposite mode chords after current?", &fcsc_cfg.oppositeChordsAfterCurMode);
		ImGui::SameLine();
		HelpMarker("If checked, when displaying all chords, the opposite mode chords will be displayed after the current ones, instead of in number order");
		ImGui::Checkbox("Swap borrowed chords when copying?", &fcsc_cfg.swapBorrowedChords);
		ImGui::SameLine();
		HelpMarker("If checked, when copying chords from one mode to the other, chords borrowed from the opposite mode will be swapped as well.");
		ImGui::Text("Disc default gain values:");

		ImGui::PushItemWidth(125);
		ImGui::InputFloat("##BeatDiscGain", &fcsc_cfg.DG0, 0.0f, 0.0f, "%.2f"); 
		ImGui::SameLine();
		ImGui::InputFloat("##BassDiscGain", &fcsc_cfg.DG1, 0.0f, 0.0f, "%.2f"); 
		ImGui::SameLine();
		ImGui::InputFloat("##LoopDiscGain", &fcsc_cfg.DG2, 0.0f, 0.0f, "%.2f"); 
		ImGui::SameLine();
		ImGui::InputFloat("##LeadDiscGain", &fcsc_cfg.DG3, 0.0f, 0.0f, "%.2f"); 
		ImGui::PopItemWidth();
		ImGui::Text("Riser default gain values:"); 
		ImGui::PushItemWidth(125);
		ImGui::InputFloat("##BeatRiserGain", &fcsc_cfg.RG0, 0.0f, 0.0f, "%.2f");
		ImGui::SameLine();
		ImGui::InputFloat("##BassRiserGain", &fcsc_cfg.RG1, 0.0f, 0.0f, "%.2f");
		ImGui::SameLine();
		ImGui::InputFloat("##LoopRiserGain", &fcsc_cfg.RG2, 0.0f, 0.0f, "%.2f");
		ImGui::SameLine();
		ImGui::InputFloat("##LeadRiserGain", &fcsc_cfg.RG3, 0.0f, 0.0f, "%.2f");
		ImGui::PopItemWidth();
		ImGui::EndChild();
		ImGui::BeginChild("Buttons", ImVec2(600, 25));
		if (ImGui::Button("OK", ImVec2(200, 0)))
		{
			if (fcsc_cfg.defaultShortName.length() > 0) {
				fcsc_cfg.saveConfig(fcsc_cfg.path);
			}
			else {
				fcsc_cfg.defaultShortName = "custom_song";
				fcsc_cfg.saveConfig(fcsc_cfg.path);
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(200, 0)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Apply", ImVec2(200, 0)))
		{
			if (fcsc_cfg.defaultShortName.length() > 0) {
				fcsc_cfg.saveConfig(fcsc_cfg.path);
			}
			else {
				fcsc_cfg.defaultShortName = "custom_song";
				fcsc_cfg.saveConfig(fcsc_cfg.path);
			}
			
		}
		ImGui::EndChild();


		ImGui::EndPopup();
	}

	ImGui::End();
	ImGui::PopStyleVar();
	closePressed = false;
}