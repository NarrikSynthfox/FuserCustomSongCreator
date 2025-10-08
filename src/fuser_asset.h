#pragma once

#include "core_types.h"

#include <iostream>

struct PlayableAudio {
	std::vector<u8> oggData;
	u64 audioHandle = 0;
	u64 channelHandle = 0;
};

struct FuserEnums {
	template<typename Enum>
	static inline typename Enum::Value ToValue(const std::string &str) {
		typename Enum::Value v = static_cast<typename Enum::Value>(0);
		size_t idx = 0;
		for (auto &&s : Enum::GetValues()) {
			if (str == s) {
				v = static_cast<typename Enum::Value>(idx);
				break;
			}
			++idx;
		}

		return v;
	}

	template<typename Enum>
	static inline const std::string &FromValue(typename Enum::Value v) {
		return Enum::GetValues()[static_cast<size_t>(v)];
	}

	struct Genre {
		enum class Value {
			None,
			Num,
			Classical,
			Country,
			Rock,
			LatinAndCaribbean,
			Pop,
			RnB,
			HipHop,
			Dance
		};

		static const std::vector<std::string>& GetValues() {
			static std::vector<std::string> values = {
				"EGenre::None",
				"EGenre::Num",
				"EGenre::Classical",
				"EGenre::Country",
				"EGenre::Rock",
				"EGenre::LatinAndCaribbean",
				"EGenre::Pop",
				"EGenre::RnB",
				"EGenre::HipHop",
				"EGenre::Dance"
			};
			return values;
		}
	};

	struct Key {
		static const std::vector<std::string>& GetValues() {
			static std::vector<std::string> values = {
				"EKey::C",
				"EKey::Db",
				"EKey::D",
				"EKey::Eb",
				"EKey::E",
				"EKey::F",
				"EKey::Gb",
				"EKey::G",
				"EKey::Ab",
				"EKey::A",
				"EKey::Bb",
				"EKey::B",
				"EKey::Num"
			};
			return values;
		}
	};

	struct KeyMode {
		enum class Value {
			Major,
			Minor,
			Num
		};

		static const std::vector<std::string>& GetValues() {
			static std::vector<std::string> values = {
				"EKeyMode::Major",
				"EKeyMode::Minor",
				"EKeyMode::Num"
			};
			return values;
		}
	};

	struct Instrument {
		static const std::vector<std::string>& GetValues() {
			static std::vector<std::string> values = {
				"EInstrument::None",
				"EInstrument::Guitar",
				"EInstrument::Drums",
				"EInstrument::Vocal",
				"EInstrument::Synth",
				"EInstrument::Sampler",
				"EInstrument::Horns",
				"EInstrument::Strings"
			};
			return values;
		}
	};
};

struct CelType {
	enum class Type {
		Beat,
		Bass,
		Loop,
		Lead
	};
	Type value;

	const std::string& getSuffix() {
		static std::string suffix_beat = "bt";
		static std::string suffix_bass = "bs";
		static std::string suffix_lead = "ld";
		static std::string suffix_loop = "lp";

		std::string typeStr;
		switch (value) {
		case Type::Beat:
			return suffix_beat;

		case Type::Bass:
			return suffix_bass;

		case Type::Lead:
			return suffix_lead;

		case Type::Loop:
			return suffix_loop;
		};

		static std::string null;
		return null;
	}

	std::string suffix(const std::string &name) {
		return name + getSuffix();
	}

	const std::string &getEnumValue() {
		static std::string enum_beat = "ECelType::Beat";
		static std::string enum_bass = "ECelType::Bass";
		static std::string enum_lead = "ECelType::Lead";
		static std::string enum_loop = "ECelType::Loop";

		switch (value) {
		case Type::Beat:
			return enum_beat;

		case Type::Bass:
			return enum_bass;

		case Type::Lead:
			return enum_lead;

		case Type::Loop:
			return enum_loop;
		};

		static std::string null;
		return null;
	}

	const std::string &getString() {
		static std::string enum_beat = "Beat";
		static std::string enum_bass = "Bass";
		static std::string enum_lead = "Lead";
		static std::string enum_loop = "Loop";

		switch (value) {
		case Type::Beat:
			return enum_beat;

		case Type::Bass:
			return enum_bass;

		case Type::Lead:
			return enum_lead;

		case Type::Loop:
			return enum_loop;
		};

		static std::string null;
		return null;
	}
};

enum class MidiType {
	Major,
	Minor
};

struct SongSerializationCtx {
	bool loading = true;

	std::string shortName;
	std::string songName;
	std::string songKey;
	std::string songGenre;
	i32 bpm;
	CelType curType;
	MidiType curMidiType;
	FuserEnums::KeyMode::Value curKeyMode;
	FuserEnums::Genre::Value genre;
	i32 year;
	bool isTransition = false;
	bool isStreamOptimized = false;
	bool smallArt = false;

	std::string folderRoot() {
		return "Audio/Songs/" + shortName + "/";
	}

	std::string artRoot() {
		return "UI/AlbumArt/";
	}

	std::string isSmallArt() {
		if (smallArt) {
			return "_small";
		}
		return "";
	}

	std::string subCelFolder() {
		return curType.suffix(shortName) + "/";
	}

	std::string subCelName() {
		return curType.suffix(shortName) + (isTransition ? "_trans" : "");
	}

	std::string midiSuffix() {
		return (curMidiType == MidiType::Major ? "_maj" : "_min");
	}

	PakFile *pak = nullptr;
	PakFile::PakEntry *curEntry = nullptr;


	PakFile::PakEntry *getFile(const std::string &fullPath) {
		for (auto &&e : pak->entries) {
			if (auto data = std::get_if<PakFile::PakEntry::PakAssetData>(&e.data)) {
				if (e.name.find(fullPath) != std::string::npos) {
					return &e;
				}
			}
		}

		return nullptr;
	}

	AssetHeader &getHeader(PakFile::PakEntry *entry = nullptr) {
		if (entry == nullptr) entry = curEntry;

		auto &&assetData = std::get<PakFile::PakEntry::PakAssetData>(entry->data);
		return std::get<AssetHeader>(assetData.pakHeader->data);
	}

	template<typename T>
	T* getProp(PakFile::PakEntry *entry, const std::string &propName) {
		auto &&assetData = std::get<PakFile::PakEntry::PakAssetData>(entry->data);
		AssetHeader *header = &std::get<AssetHeader>(assetData.pakHeader->data);
		auto &&obj = std::get<UObject>(assetData.data.catagoryValues[0].value);

		auto v = obj.data.get(header, propName);
		if (!v) {
			return nullptr;
		}

		return &std::get<T>(v->value);
	}

	template<typename T>
	struct NewProp {
		PropertyData *propData;
		T* prop;
	};

	template<typename T>
	NewProp<T> getOrCreateProp(PakFile::PakEntry *entry, const std::string &propName) {
		auto &&assetData = std::get<PakFile::PakEntry::PakAssetData>(entry->data);
		AssetHeader *header = &std::get<AssetHeader>(assetData.pakHeader->data);
		auto &&obj = std::get<UObject>(assetData.data.catagoryValues[0].value);

		auto v = obj.data.get(header, propName);
		if (!v) {
			T v;

			PropertyData prop;
			prop.nameRef = header->findOrCreateName(propName);
			prop.widgetData = 0;
			prop.typeRef = header->findOrCreateName(asset_helper::getTypeForValue(v));
			prop.value = std::move(v);
			prop.length = sizeof(T);

			obj.data.properties.emplace_back(std::move(prop));

			NewProp<T> p;
			p.propData = &obj.data.properties.back();
			p.prop = &std::get<T>(obj.data.properties.back().value);
			return p;
		}

		NewProp<T> p;
		p.propData = v;
		p.prop = &std::get<T>(v->value);
		return p;
	}

	template<typename T>
	T* getProp(const std::string &propName) {
		return getProp<T>(curEntry, propName);
	}

	template<typename T>
	NewProp<T> getOrCreateProp(const std::string &propName) {
		return getOrCreateProp<T>(curEntry, propName);
	}

	void serializeName(const std::string &propName, std::string &serializedStr) {
		auto &&prop = *getProp<NameProperty>(curEntry, propName);
		if (loading) { 
			serializedStr = prop.name.getString(getHeader());
		}
		else {
			getHeader().names[prop.name.ref].name = serializedStr;
		}
	}

	void serializeEnum(const std::string &propName, std::string &serializedStr) {
		auto &&prop = *getProp<EnumProperty>(curEntry, propName);
		if (loading) {
			serializedStr = prop.value.getString(getHeader());
		}
		else {
			prop.value = getHeader().findOrCreateName(serializedStr);
		}
	}

	void serializeString(const std::string &propName, std::string &serializedStr) {
		auto &&prop = *getProp<StringProperty>(curEntry, propName);
		if (loading) {
			serializedStr = prop.str;
		}
		else {
			prop.str = serializedStr;
		}
	}

	void serializeText(const std::string &propName, std::string &serializedStr) {
		auto &&prop = *getProp<TextProperty>(curEntry, propName);
		if (loading) {
			serializedStr = prop.strings.back();
		}
		else {
			prop.strings.back() = serializedStr;
		}
	}

	template<typename T>
	void serializePrimitive(const std::string &propName, T& value) {
		auto &&prop = *getProp<PrimitiveProperty<T>>(curEntry, propName);
		if (loading) {
			value = prop.data;
		}
		else {
			prop.data = value;
		}
	}


};

static const std::string Game_Prefix = "/Game/";

struct SongPakEntry {
	PakFile::PakEntry *e = nullptr;
	std::string path;
	std::string name;

	//@REVISIT: This is pretty janky, since exceptions need to be manually set. 
	//But I don't know if there's a better way to find this through the object structure.
	i32 thisObjectPath = 0;

	void serialize(SongSerializationCtx &ctx, const std::string &parentPath, const std::string &fileName) {
		if (ctx.loading) {
			e = ctx.getFile(parentPath + fileName);
		}
		else {
			auto &&header = e->getData().pakHeader->getHeader();

			path = parentPath + fileName;
			name = fileName;

			e->name = path + ".uexp";
			std::get<PakFile::PakEntry::PakAssetData>(e->data).pakHeader->name = path + ".uasset";

			header.names[header.catagories[0].objectName].name = fileName;

			if (thisObjectPath != -1) {
				header.names[thisObjectPath].name = Game_Prefix + parentPath + fileName;
			}
		}
	}
};

template<typename T>
struct FileLink {
	i64 linkVal;
	T data;

	void serialize(SongSerializationCtx &ctx) {
		auto prevFile = ctx.curEntry;
		auto &&header = ctx.getHeader();

		if (ctx.loading) {

			auto &&linkedFile = header.getLinkRef(header.getLinkRef(linkVal).link);
			data.file.e = ctx.getFile(header.getHeaderRef(linkedFile.property).substr(Game_Prefix.size()) + ".uexp");
		}

		if (data.file.e == nullptr) return;
		ctx.curEntry = data.file.e;
		data.serialize(ctx);
		ctx.curEntry = prevFile;

		if (!ctx.loading) {
			ctx.getHeader().names[ctx.getHeader().getLinkRef(linkVal).property].name = fs::path(data.file.path).stem().string();

			auto &&linkedFile = header.getLinkRef(header.getLinkRef(linkVal).link);
			ctx.getHeader().names[linkedFile.property].name = Game_Prefix + data.file.path;
		}
	}
};

template<typename T>
struct AssetLink {
	T data;
	StringRef32 ref;
	std::optional<StringRef32> refWithoutExtension;
	bool hasExt = false;
	std::optional<StringRef32> fullRef;
	std::optional<StringRef32> shortRef;

	void serialize(SongSerializationCtx &ctx) {
		auto prevFile = ctx.curEntry;
		auto &&header = ctx.getHeader();

		if (ctx.loading) {
			std::string fullPath = ref.getString(header);
			auto pos = fullPath.rfind('.');
			std::string assetPath = fullPath.substr(0, pos);
			hasExt = pos != std::string::npos;
			if (hasExt) {
				size_t idx = 0;
				for (auto &&s : header.names) {
					if (s.name == assetPath) {
						refWithoutExtension = StringRef32();
						refWithoutExtension->ref = idx;
					}
					++idx;
				}
			}
			//std::string assetName = fullPath.substr(pos + 1);
			data.file.e = ctx.getFile(assetPath.substr(Game_Prefix.size()) + ".uexp");

			for (auto &&l : header.links) {
				if (l.link != 0 && header.names[header.getLinkRef(l.link).property].name == assetPath) {
					shortRef = StringRef32();
					shortRef->ref = l.property;
				}
			}
		}

		if (data.file.e == nullptr) return;
		ctx.curEntry = data.file.e;
		data.serialize(ctx);
		ctx.curEntry = prevFile;

		if (!ctx.loading) {
			auto &&assetData = std::get<PakFile::PakEntry::PakAssetData>(data.file.e->data);
			auto &&subHeader = std::get<AssetHeader>(assetData.pakHeader->data);

			std::string assetPath = Game_Prefix + data.file.path;

			//@TODO: Another special case for beats
			if (refWithoutExtension) {
				header.names[refWithoutExtension->ref].name = assetPath;
			}

			if (hasExt) {
				assetPath += "." + subHeader.getHeaderRef(subHeader.catagories[0].objectName);
			}
			header.names[ref.ref].name = assetPath;

			if (shortRef.has_value()) {
				std::string shortName = assetPath.substr(assetPath.find_last_of('/') + 1);
				header.names[shortRef->ref].name = shortName;
			}
		}
	}
};


//////////

struct IconFileAsset {
	SongPakEntry file;

	void serialize(SongSerializationCtx& ctx) {
		if (!ctx.loading) {
			file.serialize(ctx, ctx.artRoot(), "T_"+ctx.shortName+ctx.isSmallArt());
			auto&& texture = std::get<Texture2D>(file.e->getData().data.catagoryValues[0].value);
		}
	}
};

struct MidiFileAsset {
	SongPakEntry file;
	bool minor = false;
	void serialize(SongSerializationCtx &ctx) {
		auto&& hmxAsset = std::get<HmxAssetFile>(file.e->getData().data.catagoryValues[0].value);
		auto& mfr = std::get<HmxAudio::PackageFile::MidiFileResource>(hmxAsset.audio.audioFiles[0].resourceHeader);
		mfr.minor = minor;
		if (!ctx.loading) {
			file.serialize(ctx, ctx.folderRoot() + ctx.subCelFolder() + "midi/", ctx.subCelName() + "_mid" + ctx.midiSuffix());
			hmxAsset.originalFilename = file.name;
			hmxAsset.audio.audioFiles[0].fileName = Game_Prefix + file.path + ".mid";
		}
		
	}
};

struct FusionFileAsset {
	SongPakEntry file;

	std::vector<PlayableAudio> playableMoggs;

	void serialize(SongSerializationCtx &ctx) {

		if (!ctx.loading) {
			file.serialize(ctx, ctx.folderRoot() + ctx.subCelFolder() + "patches/", ctx.subCelName() + "_fusion");

			auto &&asset = std::get<HmxAssetFile>(file.e->getData().data.catagoryValues[0].value);
			asset.originalFilename = ctx.subCelName() + "_fusion";

			std::vector<HmxAudio::PackageFile*> moggFiles;
			HmxAudio::PackageFile *fusionFile;

			for (auto &&file : asset.audio.audioFiles) {
				if (file.fileType == "FusionPatchResource") {
					fusionFile = &file;
				}
				else if (file.fileType == "MoggSampleResource") {
					moggFiles.emplace_back(&file);
				}
			}
			auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionFile->resourceHeader);
			auto map = fusion.nodes.getNode("keymap");
			if (fusion.nodes.getChild("audio_labels") == nullptr) {
				hmx_fusion_node audiolabelholder;
				audiolabelholder.key = "audio_labels";
				audiolabelholder.value = new hmx_fusion_nodes;
				auto& alnodes = std::get<hmx_fusion_nodes*>(audiolabelholder.value);
				int idx = 0;
				for (auto& f : moggFiles) {
					size_t found1 = f->fileName.rfind("_");
					size_t found2 = f->fileName.rfind(".mogg");
					std::string key = f->fileName.substr(found1, found2 - found1);
					hmx_fusion_node audiolabel;
					audiolabel.key = ctx.subCelName() + key;
					audiolabel.value = ctx.subCelName() + key;
					alnodes->children.emplace_back(audiolabel);
				}
				fusion.nodes.children.insert(fusion.nodes.children.begin(), audiolabelholder);
			}
			auto& audioLabels = fusion.nodes.getNode("audio_labels");
			int testidx = 0;
			for (auto& label : audioLabels.children) {
				
				size_t found1 = label.key.rfind("_");
				label.key = ctx.subCelName() + label.key.substr(found1, label.key.length());
				testidx++;
			}
			for (auto &&f : moggFiles) {
				size_t found1 = f->fileName.rfind("_");
				size_t found2 = f->fileName.rfind(".mogg");
				std::string key = f->fileName.substr(found1, found2 - found1);

				f->fileName = "C:/" + ctx.subCelName() + key + ".mogg";
				auto &&moggHeader = std::get<HmxAudio::PackageFile::MoggSampleResourceHeader>(f->resourceHeader);			
			}
			fusionFile->fileName = Game_Prefix + file.path + ".fusion";
			for (auto c : map.children) {
				auto nodes = std::get<hmx_fusion_nodes*>(c.value);


				size_t found1 = nodes->getString("sample_path").rfind("_");
				size_t found2 = nodes->getString("sample_path").rfind(".mogg");
				std::string key = nodes->getString("sample_path").substr(found1, found2 - found1);
				nodes->getString("sample_path") = "C:/" + ctx.subCelName() + key + ".mogg";

				auto&& ts = nodes->getNode("timestretch_settings");
				if (ts.getChild("orig_tempo_sync") == nullptr) {
					hmx_fusion_node label;
					label.key = "orig_tempo_sync";
					label.value = 1;
					ts.children.insert(ts.children.begin(), label);
					ts.getInt("orig_tempo") = ctx.bpm;
				}
				else {
					if (ts.getInt("orig_tempo_sync") == 1) {
						ts.getInt("orig_tempo") = ctx.bpm;
					}
				}
				
			}
		}
	}
};

struct MidiSongAsset {
	SongPakEntry file;
	bool major = false;

	AssetLink<MidiFileAsset> midiFile;
	AssetLink<FusionFileAsset> fusionFile;

	void serialize(SongSerializationCtx &ctx) {
		auto &&hmxAsset = std::get<HmxAssetFile>(file.e->getData().data.catagoryValues[0].value);
		auto &&midiMusic = std::get<HmxAudio::PackageFile::MidiMusicResource>(hmxAsset.audio.audioFiles[0].resourceHeader);
		if (ctx.loading) {
			auto fusionPath = midiMusic.patch_engine_path.str;
			fusionPath = fusionPath.substr(0, fusionPath.find_first_of('.'));

			auto midiPath = midiMusic.mid_engine_path.str;
			midiPath = midiPath.substr(0, midiPath.find_first_of('.'));

			auto &&header = file.e->getData().pakHeader->getHeader();
			for (auto &&l : header.links) {
				if (header.getHeaderRef(l.property) == fusionPath) {
					fusionFile.ref.ref = l.property;
				}

				if (header.getHeaderRef(l.property) == midiPath) {
					midiFile.ref.ref = l.property;
				}
			}

		}

		ctx.curMidiType = major ? MidiType::Major : MidiType::Minor;
		midiFile.data.minor = !major;
		midiFile.serialize(ctx);
		fusionFile.serialize(ctx);

		if (!ctx.loading) {
			std::string fileName = ctx.subCelName() + "_midisong" + (ctx.curMidiType == MidiType::Major ? "_maj" : "_min");
			file.serialize(ctx, ctx.folderRoot() + ctx.subCelFolder(), fileName);

			hmxAsset.originalFilename = fileName;
			hmxAsset.audio.audioFiles[0].fileName = Game_Prefix + file.path + ".midisong";

			midiMusic.mid_engine_path.str = Game_Prefix + midiFile.data.file.path + ".mid";
			midiMusic.patch_engine_path.str = Game_Prefix + fusionFile.data.file.path + ".fusion";
			midiMusic.midisong_engine_path.str = Game_Prefix + file.path + ".midisong";
			midiMusic.midisong_name.str = fileName + ".midisong";
			midiMusic.root.children[0].getArray().children[1].getString().str = "midi/" + midiFile.data.file.name + ".mid";
			midiMusic.root.children[1].getArray().children[1].getArray().children[2].getArray().children[1].getString().str = "patches/" + fusionFile.data.file.name + ".fusion";
		}
	}
};

struct SongTransition {
	SongPakEntry file;

	std::string shortName;
	std::vector<AssetLink<MidiSongAsset>> majorAssets;
	std::vector<AssetLink<MidiSongAsset>> minorAssets;
	float trackGain = 0;
	bool allUnpitched = false;

	void serialize(SongSerializationCtx &ctx) {
		ctx.isTransition = true;

		shortName = ctx.curType.suffix(ctx.shortName) + "_trans";
		ctx.serializeName("ShortName", shortName);

		if (ctx.loading) {
			for (auto &&v : ctx.getProp<ArrayProperty>("MajorMidiSongAssets")->values) {
				AssetLink<MidiSongAsset> midiAsset;
				midiAsset.ref = std::get<SoftObjectProperty>(v->v).name;
				midiAsset.data.major = true;
				midiAsset.serialize(ctx);
				majorAssets.emplace_back(std::move(midiAsset));
			}


			for (auto &&v : ctx.getProp<ArrayProperty>("MinorMidiSongAssets")->values) {
				AssetLink<MidiSongAsset> midiAsset;
				midiAsset.ref = std::get<SoftObjectProperty>(v->v).name;
				midiAsset.data.major = false;
				midiAsset.serialize(ctx);
				minorAssets.emplace_back(std::move(midiAsset));
			}

		}

		if (!ctx.loading) {
			ctx.serializeText("Title", ctx.songName);

			i32 bpm = ctx.bpm;
			while (bpm > 157) bpm = std::ceil(bpm /= 2); //half-time anything faster, recursively
			if (bpm < 90) bpm = ctx.bpm; //prevent 158-179 bpm from clamping to 90
			bpm = std::clamp(bpm, 90, 157);
			ctx.serializePrimitive("BPM", bpm);

			if (!allUnpitched) {
				ctx.serializeEnum("Key", ctx.songKey);
			}
			else {
				ctx.serializeEnum("Key", std::string("EKey::Num"));
			}
			auto prop = ctx.getOrCreateProp<EnumProperty>("Mode");
			prop.propData->typeRef = ctx.getHeader().findOrCreateName("EnumProperty");
			prop.propData->length = 8;
			prop.prop->enumType = ctx.getHeader().findOrCreateName("EKeyMode");
			prop.prop->blank = 0;
			if (!allUnpitched) {
				prop.prop->value = ctx.getHeader().findOrCreateName(FuserEnums::FromValue<FuserEnums::KeyMode>(ctx.curKeyMode));
			}
			else {
				prop.prop->value = ctx.getHeader().findOrCreateName(FuserEnums::FromValue<FuserEnums::KeyMode>(FuserEnums::KeyMode::Value::Num));
			}
			
			//Construct Transpose Table
			if (!allUnpitched){
				struct Transpose {
					PropertyData* data;
				};
				std::vector<Transpose> tpose;

				auto&& transposes = *ctx.getProp<StructProperty>("Transposes");
				for (auto&& v : transposes.values) {
					for (auto&& p : std::get<IPropertyDataList*>(v->v)->properties) {
						Transpose t;
						t.data = &p;
						tpose.emplace_back(std::move(t));
					}
				}

				auto&& keyValues = FuserEnums::Key::GetValues();
				auto find_idx = [&](const std::string& refKey) {
					for (i32 i = 0; i < keyValues.size(); ++i) {
						if (keyValues[i] == refKey) {
							return i;
						}
					}

					return -1;
				};

				//Check to see if we find ourself in the list (this means our key has changed)
				for (auto&& p : tpose) {
					std::string key = "EKey::" + p.data->nameRef.getString(ctx.getHeader());
					if (key == ctx.songKey) {

						size_t missingValue = 0;
						for (size_t i = 0; i < keyValues.size(); ++i) {

							bool found = false;
							for (auto&& p : tpose) {
								std::string refKey = "EKey::" + p.data->nameRef.getString(ctx.getHeader());
								if (keyValues[i] == refKey) {
									found = true;
									break;
								}
							}

							if (!found) {
								missingValue = i;
								break;
							}
						}

						p.data->nameRef = ctx.getHeader().findOrCreateName(keyValues[missingValue].substr(sizeof("EKey::") - 1));
						break;
					}
				}

				//Now we can compute the offsets
				i32 songKeyIdx = find_idx(ctx.songKey);
				for (auto&& p : tpose) {
					std::string key = "EKey::" + p.data->nameRef.getString(ctx.getHeader());
					i32 idx = find_idx(key);
					i32 offset = idx - songKeyIdx;
					if (offset <= -6) {
						offset += 12;
					}
					else if (offset >= 6) {
						offset -= 12;
					}
					std::get<PrimitiveProperty<i32>>(p.data->value).data = offset;
				}

			}

			for (auto&& a : majorAssets) {
				a.serialize(ctx);
			}

			for (auto&& a : minorAssets) {
				a.serialize(ctx);
			}

			file.serialize(ctx, ctx.folderRoot() + ctx.subCelFolder(), "Meta_" + ctx.subCelName());
		}

		ctx.isTransition = false;
	}
};

struct CelData {
	SongPakEntry file;
	bool tickLengthAdvanced = false;
	int selectedTickLength = 2;
	CelType type;
	FuserEnums::KeyMode::Value mode = FuserEnums::KeyMode::Value::Major;
	std::string shortName;
	std::string instrument;
	//std::optional<std::string> subInstrument;
	FileLink<SongTransition> songTransitionFile;
	std::vector<AssetLink<MidiSongAsset>> majorAssets;
	std::vector<AssetLink<MidiSongAsset>> minorAssets;
	ArrayProperty* pickupArray;
	float trackGain = 0;
	int tickLength = 61440;

	bool allUnpitched = false;

	//std::vector<float> pickupsTest;
	void serialize(SongSerializationCtx &ctx) {
		if (ctx.loading) {
			//for (auto v : ctx.getProp<ArrayProperty>("PickupBeats")->values) {
			//	pickupsTest.emplace_back(std::get<PrimitiveProperty<float>>(v->v).data);
			//}
			auto celType = ctx.getProp<EnumProperty>(ctx.curEntry, "CelType");
			if (celType == nullptr) {
				type.value = CelType::Type::Beat; //Beat is the first value, and therefore default of the enum, so it isn't serialized in the file.
			}
			else {
				auto celTypeStr = celType->value.getString(ctx.getHeader());
				if (celTypeStr == "ECelType::Beat") {
					type.value = CelType::Type::Beat;
				}
				else if (celTypeStr == "ECelType::Bass") {
					type.value = CelType::Type::Bass;
				}
				else if (celTypeStr == "ECelType::Lead") {
					type.value = CelType::Type::Lead;
				}
				else if (celTypeStr == "ECelType::Loop") {
					type.value = CelType::Type::Loop;
				}
			}

			
			if (auto celMode = ctx.getProp<EnumProperty>(ctx.curEntry, "Mode")) {
				auto celModeStr = celMode->value.getString(ctx.getHeader());
				mode = FuserEnums::ToValue<FuserEnums::KeyMode>(celModeStr);
				if (mode == FuserEnums::KeyMode::Value::Num) {
					allUnpitched = true;
					songTransitionFile.data.allUnpitched = true;
				}
			}
			if (auto tlprop = ctx.getProp<PrimitiveProperty<int>>("SongLengthTicks")) {
				tickLength=tlprop->data;
			}
				
		}

		shortName = ctx.shortName + type.getSuffix();
		ctx.serializeName("ShortName", shortName);
		ctx.curType = type;

		ctx.serializeEnum("Instrument", instrument);
		//ctx.serializeEnum("SubInstrument", subInstrument);
		
		//@REVISIT
		file.thisObjectPath = 4;
		songTransitionFile.data.file.thisObjectPath = 4;


		songTransitionFile.linkVal = ctx.getProp<ObjectProperty>("SongTransitionCelData")->linkVal;
		songTransitionFile.serialize(ctx);

		if (ctx.loading) {
			if (tickLength == 15360) {
				selectedTickLength = 0;
			}else if (tickLength == 30720) {
				selectedTickLength = 1;
			}else if (tickLength == 61440) {
				selectedTickLength = 2;
			}else if (tickLength == 122880) {
				selectedTickLength = 3;
			}
			else {
				tickLengthAdvanced = true;
			}
			for (auto &&v : ctx.getProp<ArrayProperty>("MajorMidiSongAssets")->values) {
				AssetLink<MidiSongAsset> midiAsset;
				midiAsset.ref = std::get<SoftObjectProperty>(v->v).name;
				midiAsset.data.major = true;
				midiAsset.serialize(ctx);
				majorAssets.emplace_back(std::move(midiAsset));
			}

			for (auto &&v : ctx.getProp<ArrayProperty>("MinorMidiSongAssets")->values) {
				AssetLink<MidiSongAsset> midiAsset;
				midiAsset.ref = std::get<SoftObjectProperty>(v->v).name;
				midiAsset.data.major = false;
				midiAsset.serialize(ctx);
				minorAssets.emplace_back(std::move(midiAsset));
			}

			pickupArray = ctx.getProp<ArrayProperty>("PickupBeats");
			
		}

		if (!ctx.loading) {
			ctx.serializeText("Title", ctx.songName);
			
			i32 bpm = ctx.bpm;
			while (bpm > 157) bpm = std::ceil(bpm /= 2); //half-time anything faster, recursively
			if (bpm < 90) bpm = ctx.bpm; //prevent 158-179 bpm from clamping to 90
			bpm = std::clamp(bpm, 90, 157);
			ctx.serializePrimitive("BPM", bpm);

			
			if (!allUnpitched) {
				ctx.serializeEnum("Key", ctx.songKey);
			}
			else {
				ctx.serializeEnum("Key", std::string("EKey::Num"));
			}
			
			auto prop = ctx.getOrCreateProp<EnumProperty>("Mode");
			prop.propData->typeRef = ctx.getHeader().findOrCreateName("EnumProperty");
			prop.propData->length = 8;
			prop.prop->enumType = ctx.getHeader().findOrCreateName("EKeyMode");
			prop.prop->blank = 0;
			if (!allUnpitched) {
				prop.prop->value = ctx.getHeader().findOrCreateName(FuserEnums::FromValue<FuserEnums::KeyMode>(ctx.curKeyMode));
			}
			else {
				prop.prop->value = ctx.getHeader().findOrCreateName(FuserEnums::FromValue<FuserEnums::KeyMode>(FuserEnums::KeyMode::Value::Num));
			}
			
			auto tlprop = ctx.getOrCreateProp<PrimitiveProperty<int>>("SongLengthTicks");
			tlprop.prop->data = tickLength;

			if (!allUnpitched) {
				struct Transpose {
					PropertyData *data;
				};
				std::vector<Transpose> tpose;

				auto&& transposes = *ctx.getProp<StructProperty>("Transposes");
				for (auto &&v : transposes.values) {
					for (auto &&p : std::get<IPropertyDataList*>(v->v)->properties) {
						Transpose t;
						t.data = &p;
						tpose.emplace_back(std::move(t));
					}
				}

				auto&& keyValues = FuserEnums::Key::GetValues();
				auto find_idx = [&](const std::string &refKey) {
					for (i32 i = 0; i < keyValues.size(); ++i) {
						if (keyValues[i] == refKey) {
							return i;
						}
					}

					return -1;
				};

				//Check to see if we find ourself in the list (this means our key has changed)
				for (auto &&p : tpose) {
					std::string key = "EKey::" + p.data->nameRef.getString(ctx.getHeader());
					if (key == ctx.songKey) {

						size_t missingValue = 0;
						for (size_t i = 0; i < keyValues.size(); ++i) {

							bool found = false;
							for (auto &&p : tpose) {
								std::string refKey = "EKey::" + p.data->nameRef.getString(ctx.getHeader());
								if (keyValues[i] == refKey) {
									found = true;
									break;
								}
							}

							if (!found) {
								missingValue = i;
								break;
							}
						}

						p.data->nameRef = ctx.getHeader().findOrCreateName(keyValues[missingValue].substr(sizeof("EKey::") - 1));
						break;
					}
				}

				//Now we can compute the offsets
				i32 songKeyIdx = find_idx(ctx.songKey);
				for (auto &&p : tpose) {
					std::string key = "EKey::" + p.data->nameRef.getString(ctx.getHeader());
					i32 idx = find_idx(key);
					i32 offset = idx - songKeyIdx;
					if (offset <= -6) {
						offset += 12;
					}
					else if (offset >= 6) {
						offset -= 12;
					}
					std::get<PrimitiveProperty<i32>>(p.data->value).data = offset;
				}

			}

			for (auto &&a : majorAssets) {
				a.serialize(ctx);
			}

			for (auto &&a : minorAssets) {
				a.serialize(ctx);
			}

			file.serialize(ctx, ctx.folderRoot() + ctx.subCelFolder(), "Meta_" + type.suffix(ctx.shortName));
		}
	}
};

struct AssetRoot {
	std::string shortName;
	std::string artistName;
	std::string songName;
	std::string songKey;
	bool isStreamOptimized;
	FuserEnums::KeyMode::Value keyMode;
	FuserEnums::Genre::Value genre;
	i32 year;
	i32 bpm;

	SongPakEntry file;

	std::vector<FileLink<CelData>> celData;
	std::vector<FileLink<Texture2D>> textureData;
	AssetLink<IconFileAsset> small_icon_link;
	AssetLink<IconFileAsset> large_icon_link;

	void serialize(SongSerializationCtx &ctx) {
		ctx.shortName = shortName;
		ctx.songName = songName;
		ctx.songKey = songKey;
		ctx.bpm = bpm;
		ctx.curKeyMode = keyMode;

		//@REVISIT
		file.thisObjectPath = 5;

		file.serialize(ctx, "DLC/Songs/" + ctx.shortName + "/", "Meta_" + ctx.shortName);
		ctx.curEntry = file.e;

		ctx.serializeText("Artist", artistName);
		
		ctx.serializePrimitive<i32>("Year", year);
		
		ctx.year = year;
		if (ctx.loading) {
			std::string actualKey = "EKey::C";
			FuserEnums::KeyMode::Value actualMode = FuserEnums::KeyMode::Value::Major;
			auto streamOptimizedProp= ctx.getProp<BoolProperty>("IsStreamOptimized");
			if (streamOptimizedProp != nullptr) {
				isStreamOptimized = streamOptimizedProp->value;
			}
			else {
				isStreamOptimized = false;
			}
			ctx.isStreamOptimized = isStreamOptimized;
			auto genrePtr = ctx.getProp<EnumProperty>(ctx.curEntry, "Genre");
			auto genreStr = genrePtr->value.getString(ctx.getHeader());
			genre = FuserEnums::ToValue<FuserEnums::Genre>(genreStr);
			ctx.genre = genre;
			auto icon = ctx.getProp<SoftObjectProperty>(file.e, "SongIconImage");
			small_icon_link.ref = icon->name;
			ctx.smallArt = true;
			small_icon_link.serialize(ctx);
			ctx.smallArt = false;
			icon = ctx.getProp<SoftObjectProperty>(file.e, "SongIconImage_Large");
			large_icon_link.ref = icon->name;
			large_icon_link.serialize(ctx);
			auto &celArray = *ctx.getProp<ArrayProperty>(file.e, "Cels");
			for (auto &&v : celArray.values) {
				FileLink<CelData> fileLink;
				fileLink.linkVal = std::get<ObjectProperty>(v->v).linkVal;
				fileLink.serialize(ctx);

					ctx.songName = songName = ctx.getProp<TextProperty>(fileLink.data.file.e, "Title")->strings.back();
					ctx.bpm = bpm = ctx.getProp<PrimitiveProperty<i32>>(fileLink.data.file.e, "BPM")->data;
					ctx.songKey = songKey = ctx.getProp<EnumProperty>(fileLink.data.file.e, "Key")->value.getString(fileLink.data.file.e->getHeader());
					ctx.curKeyMode = keyMode = fileLink.data.mode;
					if (ctx.songKey != "EKey::Num") {
						actualKey = ctx.songKey;
					}
					if (ctx.curKeyMode != FuserEnums::KeyMode::Value::Num) {
						actualMode = ctx.curKeyMode;
					}
				celData.emplace_back(std::move(fileLink));
			}
			ctx.songKey = songKey = actualKey;
			ctx.curKeyMode = keyMode = actualMode;
		}
		else {
			ctx.smallArt = true;
			small_icon_link.serialize(ctx);
			ctx.smallArt = false;
			large_icon_link.serialize(ctx);
			auto genreStr = FuserEnums::FromValue<FuserEnums::Genre>(genre);
			ctx.serializeName("SongShortName", shortName);
			ctx.serializeEnum("Genre", genreStr);
			auto streamOptimizedProp = ctx.getOrCreateProp<BoolProperty>("IsStreamOptimized");
			streamOptimizedProp.prop->value = isStreamOptimized;

			size_t idx = 0;
			for (auto &&e : celData) {
				e.serialize(ctx);
				++idx;
			}
		}
	}
};