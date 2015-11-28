#include "stdafx.h"

#include <type_traits>
#include <temple/dll.h>

#include <graphics/mdfmaterials.h>

#include "gamesystems.h"
#include "config/config.h"
#include "legacy.h"
#include <tig/tig_texture.h>
#include "../tig/tig_startup.h"
#include <tig/tig_mouse.h>
#include <tig/tig_loadingscreen.h>
#include <tig/tig_font.h>
#include <tig/tig_msg.h>
#include <movies.h>
#include <python/python_integration_obj.h>
#include "legacysystems.h"
#include "graphics/loadingscreen.h"
#include "mappreprocessor.h"
#include "particlesystems.h"

#include "mapsystems.h"
#include <infrastructure/vfs.h>
#include <temple/vfs.h>
#include <temple/meshes.h>
#include <infrastructure/mesparser.h>
#include <util/fixes.h>
#include <graphics/device.h>

using namespace gfx;

/*
Manages graphics engine resources used by
legacy game systems.
*/
class LegacyGameSystemResources : public ResourceListener {
public:

	explicit LegacyGameSystemResources(RenderingDevice& device)
		: mRegistration(device, this) {
	}

	void CreateResources(RenderingDevice&) override {

		gameSystemInitTable.CreateTownmapFogBuffer();
		gameSystemInitTable.CreateShadowMapBuffer();
	}

	void FreeResources(RenderingDevice&) override {

		gameSystemInitTable.GameDisableDrawingForce();
		gameSystemInitTable.ReleaseTownmapFogBuffer();
		gameSystemInitTable.ReleaseShadowMapBuffer();

	}

private:
	ResourceListenerRegistration mRegistration;
};

GameSystems::GameSystems(TigInitializer& tig) : mTig(tig) {

	if (!gameSystems) {
		gameSystems = this;
	}

	StopwatchReporter reporter("Game systems initialized in {}");
	logger->info("Loading game systems");

	memset(&mConfig, 0, sizeof(mConfig));
	mConfig.editor = ::config.editor ? 1 : 0;
	mConfig.width = tig.GetConfig().width;
	mConfig.height = tig.GetConfig().height;
	mConfig.field_10 = temple::GetPointer(0x10002530); // Callback 1
	mConfig.renderfunc = temple::GetPointer(0x10002650); // Callback 1
	mConfig.bufferstuffIdx = mTigBuffer.bufferIdx();

	config.AddVanillaSetting("difficulty", "1", gameSystemInitTable.DifficultyChanged);
	gameSystemInitTable.DifficultyChanged(); // Read initial setting
	config.AddVanillaSetting("autosave_between_maps", "1");
	config.AddVanillaSetting("movies_seen", "(304,-1)");
	config.AddVanillaSetting("startup_tip", "0");
	config.AddVanillaSetting("video_adapter", "0");
	config.AddVanillaSetting("video_width", "800");
	config.AddVanillaSetting("video_height", "600");
	config.AddVanillaSetting("video_bpp", "32");
	config.AddVanillaSetting("video_refresh_rate", "60");
	config.AddVanillaSetting("video_antialiasing", "0");
	config.AddVanillaSetting("video_quad_blending", "1");
	config.AddVanillaSetting("particle_fidelity", "100");
	config.AddVanillaSetting("env_mapping", "1");
	config.AddVanillaSetting("cloth_frame_skip", "0");
	config.AddVanillaSetting("concurrent_turnbased", "1");
	config.AddVanillaSetting("end_turn_time", "1");
	config.AddVanillaSetting("end_turn_default", "1");
	config.AddVanillaSetting("draw_hp", "0");

	// Some of these are also registered as value change callbacks and could be replaced by simply calling all 
	// value change callbacks here, which makes sense anyway.
	auto particleFidelity = config.GetVanillaInt("particle_fidelity") / 100.0f;
	gameSystemInitTable.SetParticleFidelity(particleFidelity);

	auto envMappingEnabled = config.GetVanillaInt("env_mapping");
	gameSystemInitTable.SetEnvMapping(envMappingEnabled);

	auto clothFrameSkip = config.GetVanillaInt("cloth_frame_skip");
	gameSystemInitTable.SetClothFrameSkip(clothFrameSkip);

	auto concurrentTurnbased = config.GetVanillaInt("concurrent_turnbased");
	gameSystemInitTable.SetConcurrentTurnbased(concurrentTurnbased);

	auto endTurnTime = config.GetVanillaInt("end_turn_time");
	gameSystemInitTable.SetEndTurnTime(endTurnTime);

	auto endTurnDefault = config.GetVanillaInt("end_turn_default");
	gameSystemInitTable.SetEndTurnDefault(endTurnDefault);

	auto drawHp = config.GetVanillaInt("draw_hp");
	gameSystemInitTable.SetDrawHp(drawHp != 0);

	*gameSystemInitTable.moduleLoaded = 0;

	mouseFuncs.SetBounds(mConfig.width, mConfig.height);

	// This may be redundant since it should have been set during tig initialization
	loadingScreenFuncs.SetBounds(mConfig.width, mConfig.height);

	// I don't even think partial install is an option for ToEE, but it may have been for Arcanum
	// It seems to be left-over, really since it also references Sierra in the code, while ToEE
	// was published by Atari
	// TODO: Once we reimplement the gamelib_mod_load function, this can be removed since it's only read from there
	*gameSystemInitTable.fullInstall = true;

	VerifyTemplePlusData();

	auto lang = GetLanguage();
	if (lang == "en") {
		logger->info("Assuming english fonts");
		*gameSystemInitTable.fontIsEnglish = true;
	}

	if (!config.skipLegal) {
		PlayLegalMovies();
	}

	// For whatever reason these are set here
	memcpy(gameSystemInitTable.gameSystemConf, &mConfig, sizeof(GameSystemConf));
	gameSystemInitTable.gameSystemConf->field_10 = temple::GetPointer<0x10002530>();
	gameSystemInitTable.gameSystemConf->renderfunc = temple::GetPointer<0x10002650>();

	InitBufferStuff(mConfig);

	InitAnimationSystem();

	tigFont.LoadAll("art\\interface\\fonts\\*.*");
	tigFont.PushFont("priory-12", 12, true);

	LoadingScreen loadingScreen(tig.GetRenderingDevice(), tig.GetShapeRenderer2d());
	InitializeSystems(loadingScreen);

	mLegacyResources
		= std::make_unique<LegacyGameSystemResources>(tig.GetRenderingDevice());

	gameSystemInitTable.InitPfxLightning();

	*gameSystemInitTable.ironmanFlag = false;
	*gameSystemInitTable.ironmanSaveGame = 0;

}

GameSystems::~GameSystems() {

	if (gameSystems == this) {
		gameSystems = nullptr;
	}

	logger->info("Unloading game systems");

	// Clear the loaded systems in reverse order
	mLegacyResources.reset();

	mPathX.reset();
	mItemHighlight.reset();
	mFormation.reset();
	mObjectEvent.reset();
	mRandomEncounter.reset();
	mMapFogging.reset();
	mSecretdoor.reset();
	mD20Rolls.reset();
	mCheats.reset();
	mParticleSys.reset();
	mUiArtManager.reset();
	mDeity.reset();
	mObjFade.reset();
	mGround.reset();
	mGameInit.reset();
	mD20LoadSave.reset();
	mParty.reset();
	mMonsterGen.reset();
	mTrap.reset();
	mAntiTeleport.reset();
	mGFade.reset();
	mBrightness.reset();
	mGMovie.reset();
	mTownMap.reset();
	mInvenSource.reset();
	mWP.reset();
	mSectorScript.reset();
	mTileScript.reset();
	mReaction.reset();
	mReputation.reset();
	mAnimPrivate.reset();
	mAnim.reset();
	mAI.reset();
	mQuest.reset();
	mRumor.reset();
	mTimeEvent.reset();
	mCombat.reset();
	mItem.reset();
	mSoundGame.reset();
	mSoundMap.reset();
	mDialog.reset();
	mArea.reset();
	mPlayer.reset();
	mLightScheme.reset();
	mMap.reset();
	mD20.reset();
	mLevel.reset();
	mScript.reset();
	mStat.reset();
	mSpell.reset();
	mFeat.reset();
	mSkill.reset();
	mPortrait.reset();
	mScriptName.reset();
	mCritter.reset();
	mRandom.reset();
	mSector.reset();
	mTeleport.reset();
	mItemEffect.reset();
	mDescription.reset();
	mVagrant.reset();

	mLegacyResources.reset();

}

TigBufferstuffInitializer::TigBufferstuffInitializer() {
	StopwatchReporter reporter("Game scratch buffer initialized in {}");
	logger->info("Creating game scratch buffer");
	if (!gameSystemInitTable.TigWindowBufferstuffCreate(&mBufferIdx)) {
		throw TempleException("Unable to initialize TIG buffer");
	}
}

TigBufferstuffInitializer::~TigBufferstuffInitializer() {
	logger->info("Freeing game scratch buffer");
	gameSystemInitTable.TigWindowBufferstuffFree(mBufferIdx);
}

/*
Checks that the TemplePlus data file has been loaded in some way.
*/
void GameSystems::VerifyTemplePlusData() {

	TioFileListFile info;
	if (!tio_fileexists("templeplus\\data_present", &info)) {
		throw TempleException("The TemplePlus data files are not installed.");
	}

}

// Gets the language of the current toee installation (i.e. "en")
std::string GameSystems::GetLanguage() {
	try {
		auto content(MesFile::ParseFile("mes\\language.mes"));
		if (content[1].empty()) {
			return "en";
		}
		return content[1];
	} catch (TempleException&) {
		return "en";
	}
}

void GameSystems::PlayLegalMovies() {
	movieFuncs.PlayMovie("movies\\AtariLogo.bik", 0, 0, 0);
	movieFuncs.PlayMovie("movies\\TroikaLogo.bik", 0, 0, 0);
	movieFuncs.PlayMovie("movies\\WotCLogo.bik", 0, 0, 0);
}

void GameSystems::InitBufferStuff(const GameSystemConf& conf) {

	// TODO: It is VERY dubious that this is actually used anywhere!!!
	// scratchbuffer is never accessed
	if (*gameSystemInitTable.scratchBuffer == nullptr) {
		TigBufferCreateArgs createArgs;
		createArgs.flagsOrSth = 5;
		createArgs.width = conf.width;
		createArgs.height = conf.height;
		createArgs.field_10 = 0;
		createArgs.zero4 = 0;
		createArgs.texturetype = 2;
		if (textureFuncs.CreateBuffer(&createArgs, gameSystemInitTable.scratchBuffer)) {
			throw TempleException("Unable to create the scratchbuffer!");
		}
	}

	/*auto rect = gameSystemInitTable.scratchBufferRect;
	rect->y = 0;
	rect->x = 0;
	rect->width = conf.width;
	rect->height = conf.height;*/

	// Without this rectangle in particular, the ingame view is not drawn
	auto rectExtended = gameSystemInitTable.scratchBufferRectExtended;
	rectExtended->x = -256;
	rectExtended->y = -256;
	rectExtended->width = conf.width + 512;
	rectExtended->height = conf.height + 512;

	/*
	tig_bufferstuff_get(conf->tigwindowbufferstuffidx, &tigwindowbuffer);
	bufferstuff_left = tigwindowbuffer.rect.x;
	bufferstuff_top = tigwindowbuffer.rect.y;
	These two seem definetly unused
	dword_1030705C = video_width / 4;
	dword_10307170 = video_height / 4;
	*/
}

void GameSystems::InitAnimationSystem() {
	
	mMeshesById = MesFile::ParseFile("art\\meshes\\meshes.mes");

	temple::AasConfig config;
	config.runScript = [] (const std::string &command) {
		pythonObjIntegration.RunAnimFrameScript(command);
	};
	config.resolveSkaFile = [=](int modelId) {
		return ResolveSkaFile(modelId);
	};
	config.resolveSkmFile = [=](int modelId) {
		return ResolveSkmFile(modelId);
	};
	config.resolveMaterial = [=](const std::string &material) {
		return ResolveMaterial(material);
	};

	mAAS = std::make_unique<temple::AasAnimatedModelFactory>(config);

}

std::string GameSystems::ResolveSkaFile(int meshId) const {

	auto it = mMeshesById.find(meshId);
	if (it == mMeshesById.end()) {
		return std::string();
	}

	return fmt::format("art\\meshes\\{}.ska", it->second);

}

std::string GameSystems::ResolveSkmFile(int meshId) const {

	auto it = mMeshesById.find(meshId);
	if (it == mMeshesById.end()) {
		return std::string();
	}

	return fmt::format("art\\meshes\\{}.skm", it->second);

}

int GameSystems::ResolveMaterial(const std::string& materialName) const {
	auto material(mTig.GetMdfFactory().LoadMaterial(materialName));
	if (!material) {
		logger->error("Unable to load material {}", materialName);
		return -1;
	}
	return material->GetId();
}

void GameSystems::AdvanceTime() {

	auto now = timeGetTime();

	// This is used from somewhere in the object system
	*gameSystemInitTable.lastAdvanceTime = now;

	for (auto system : mTimeAwareSystems) {
		system->AdvanceTime(now);
	}

}

void GameSystems::LoadModule(const std::string& moduleName) {

	AddModulePaths(moduleName);

	auto tioVfs = static_cast<temple::TioVfs*>(vfs.get());

	constexpr char* saveDir = "Save\\Current";
	if (vfs->DirExists(saveDir) && !tioVfs->IsDirEmpty(saveDir) && !tioVfs->CleanDir(saveDir)) {
		throw TempleException("Unable to clean current savegame folder from previous data: {}", saveDir);
	}

	MapMobilePreprocessor preprocessor(mModuleGuid);

	// Preprocess mob files for each map before we load the first map
	for (const auto& entry : vfs->Search("maps\\*.*")) {
		if (!entry.dir) {
			continue;
		}

		preprocessor.Preprocess(entry.filename);
	}

	for (auto system : mModuleAwareSystems) {
		system->LoadModule();
	}

}

void GameSystems::AddModulePaths(const std::string& moduleName) {

	std::string moduleBase;
	if (!Path::IsFileSystem(moduleName)) {
		moduleBase = Path::Concat(".\\Modules\\", moduleName);
	} else {
		moduleBase = moduleName;
	}

	auto moduleDatName = moduleBase + ".dat";
	auto moduleDir = moduleBase;

	auto tioVfs = static_cast<temple::TioVfs*>(vfs.get());

	memset(&mModuleGuid, 0, sizeof(mModuleGuid));

	logger->info("Module archive: {}", moduleDatName);
	if (vfs->FileExists(moduleDatName)) {
		if (!tioVfs->AddPath(moduleDatName)) {
			throw TempleException("Unable to load module archive {}", moduleDatName);
		}

		tioVfs->GetArchiveGUID(moduleDatName, mModuleGuid);
	}

	mModuleArchivePath = moduleDatName;

	logger->info("Module directory: {}", moduleDir);
	if (!vfs->DirExists(moduleDir)) {
		if (!vfs->MkDir(moduleDir)) {
			if (!mModuleArchivePath.empty()) {
				tioVfs->RemovePath(mModuleArchivePath);
			}
			throw TempleException("Unable to create module folder: {}", moduleDir);
		}
	}

	mModuleDirPath = moduleDir;

	// The very last folder that is in the chain seems to be the target
	// for save data. So this remains the module folder
	if (!tioVfs->AddPath(mModuleDirPath)) {
		throw TempleException("Unable to add module directory: {}", mModuleDirPath);
	}

}

void GameSystems::UnloadModule() {
	throw TempleException("Currently unsupported");
}

void GameSystems::RemoveModulePaths() {
}

void GameSystems::ResetGame() {
	logger->info("Resetting game systems...");

	mResetting = true;

	if (vfs->DirExists("Save\\Current")) {
		if (!vfs->CleanDir("Save\\Current")) {
			logger->error("Unable to clean current save game directory.");
		}
	}

	for (auto system : mResetAwareSystems) {
		logger->debug("Resetting game system {}", system->GetName());
		system->Reset();
	}

	*gameSystemInitTable.ironmanFlag = false;
	if (*gameSystemInitTable.ironmanSaveGame) {
		free(*gameSystemInitTable.ironmanSaveGame);
	}
	*gameSystemInitTable.ironmanSaveGame = nullptr;

	mResetting = false;

}

void GameSystems::ResizeScreen(int w, int h) {

	gameSystemInitTable.gameSystemConf->width = w;
	gameSystemInitTable.gameSystemConf->height = h;

	// These do not seem to be read anywhere
	// dword_1030705C = video_width / 4;
	// dword_10307170 = video_height / 4;

	// TODO: Can be removed since we write it on the stack in GameSystemsRender
	auto scratchBufferRectExtended = gameSystemInitTable.scratchBufferRectExtended;
	scratchBufferRectExtended->y = -256;
	scratchBufferRectExtended->width = w + 512;
	scratchBufferRectExtended->x = -256;
	scratchBufferRectExtended->height = h + 512;

	auto scratchBufferRect = gameSystemInitTable.scratchBufferRect;
	scratchBufferRect->x = 0;
	scratchBufferRect->y = 0;
	scratchBufferRect->width = w;
	scratchBufferRect->height = h;

	auto windowBufferStuffId = gameSystemInitTable.gameSystemConf->bufferstuffIdx;

	// I do not think that the scratchbuffer is used in any way
	// rebuild_scratchbuffer(&resizeargs);
	*gameSystemInitTable.scratchBuffer = nullptr;

	ui.ResizeScreen(windowBufferStuffId, w, h);

}

void GameSystems::InitializeSystems(LoadingScreen& loadingScreen) {

	loadingScreen.SetMessage("Loading...");

	// Loading Screen ID: 2
	loadingScreen.SetProgress(1 / 61.0f);
	mVagrant = InitializeSystem<VagrantSystem>(loadingScreen, mConfig);
	// Loading Screen ID: 2
	loadingScreen.SetProgress(2 / 61.0f);
	mDescription = InitializeSystem<DescriptionSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(3 / 61.0f);
	mItemEffect = InitializeSystem<ItemEffectSystem>(loadingScreen, mConfig);
	// Loading Screen ID: 3
	loadingScreen.SetProgress(4 / 61.0f);
	mTeleport = InitializeSystem<TeleportSystem>(loadingScreen, mConfig);
	// Loading Screen ID: 4
	loadingScreen.SetProgress(5 / 61.0f);
	mSector = InitializeSystem<SectorSystem>(loadingScreen, mConfig);
	// Loading Screen ID: 5
	loadingScreen.SetProgress(6 / 61.0f);
	mRandom = InitializeSystem<RandomSystem>(loadingScreen, mConfig);
	// Loading Screen ID: 6
	loadingScreen.SetProgress(7 / 61.0f);
	mCritter = InitializeSystem<CritterSystem>(loadingScreen, mConfig);
	// Loading Screen ID: 7
	loadingScreen.SetProgress(8 / 61.0f);
	mScriptName = InitializeSystem<ScriptNameSystem>(loadingScreen, mConfig);
	// Loading Screen ID: 8
	loadingScreen.SetProgress(9 / 61.0f);
	mPortrait = InitializeSystem<PortraitSystem>(loadingScreen, mConfig);
	// Loading Screen ID: 9
	loadingScreen.SetProgress(10 / 61.0f);
	mSkill = InitializeSystem<SkillSystem>(loadingScreen, mConfig);
	// Loading Screen ID: 10
	loadingScreen.SetProgress(11 / 61.0f);
	mFeat = InitializeSystem<FeatSystem>(loadingScreen, mConfig);
	// Loading Screen ID: 11
	loadingScreen.SetProgress(12 / 61.0f);
	mSpell = InitializeSystem<SpellSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(13 / 61.0f);
	mStat = InitializeSystem<StatSystem>(loadingScreen, mConfig);
	// Loading Screen ID: 12
	loadingScreen.SetProgress(14 / 61.0f);
	mScript = InitializeSystem<ScriptSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(15 / 61.0f);
	mLevel = InitializeSystem<LevelSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(16 / 61.0f);
	mD20 = InitializeSystem<D20System>(loadingScreen, mConfig);
	// Loading Screen ID: 1
	loadingScreen.SetProgress(17 / 61.0f);
	mMapSystems = InitializeSystem<MapSystems>(loadingScreen, mTig);
	mMap = InitializeSystem<MapSystem>(loadingScreen, mConfig);	
	loadingScreen.SetProgress(18 / 61.0f);
	mLightScheme = InitializeSystem<LightSchemeSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(19 / 61.0f);
	mPlayer = InitializeSystem<PlayerSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(20 / 61.0f);
	mArea = InitializeSystem<AreaSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(21 / 61.0f);
	mDialog = InitializeSystem<DialogSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(22 / 61.0f);
	mSoundMap = InitializeSystem<SoundMapSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(23 / 61.0f);
	mSoundGame = InitializeSystem<SoundGameSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(24 / 61.0f);
	mItem = InitializeSystem<ItemSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(25 / 61.0f);
	mCombat = InitializeSystem<CombatSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(26 / 61.0f);
	mTimeEvent = InitializeSystem<TimeEventSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(27 / 61.0f);
	mRumor = InitializeSystem<RumorSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(28 / 61.0f);
	mQuest = InitializeSystem<QuestSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(29 / 61.0f);
	mAI = InitializeSystem<AISystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(30 / 61.0f);
	mAnim = InitializeSystem<AnimSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(31 / 61.0f);
	mAnimPrivate = InitializeSystem<AnimPrivateSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(32 / 61.0f);
	mReputation = InitializeSystem<ReputationSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(33 / 61.0f);
	mReaction = InitializeSystem<ReactionSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(34 / 61.0f);
	mTileScript = InitializeSystem<TileScriptSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(35 / 61.0f);
	mSectorScript = InitializeSystem<SectorScriptSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(36 / 61.0f);

	// NOTE: This system is only used in worlded (rendering related)
	mWP = InitializeSystem<WPSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(37 / 61.0f);

	mInvenSource = InitializeSystem<InvenSourceSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(38 / 61.0f);
	mTownMap = InitializeSystem<TownMapSystem>(loadingScreen);
	loadingScreen.SetProgress(39 / 61.0f);
	mGMovie = InitializeSystem<GMovieSystem>(loadingScreen);
	loadingScreen.SetProgress(40 / 61.0f);
	mBrightness = InitializeSystem<BrightnessSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(41 / 61.0f);
	mGFade = InitializeSystem<GFadeSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(42 / 61.0f);
	mAntiTeleport = InitializeSystem<AntiTeleportSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(43 / 61.0f);
	mTrap = InitializeSystem<TrapSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(44 / 61.0f);
	mMonsterGen = InitializeSystem<MonsterGenSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(45 / 61.0f);
	mParty = InitializeSystem<PartySystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(46 / 61.0f);
	mD20LoadSave = InitializeSystem<D20LoadSaveSystem>(loadingScreen);
	loadingScreen.SetProgress(47 / 61.0f);
	mGameInit = InitializeSystem<GameInitSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(48 / 61.0f);
	mGround = InitializeSystem<GroundSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(49 / 61.0f);
	mObjFade = InitializeSystem<ObjFadeSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(50 / 61.0f);
	mDeity = InitializeSystem<DeitySystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(51 / 61.0f);
	mUiArtManager = InitializeSystem<UiArtManagerSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(52 / 61.0f);
	mParticleSys = InitializeSystem<ParticleSysSystem>(loadingScreen, 
		mTig.GetRenderingDevice().GetCamera());
	loadingScreen.SetProgress(53 / 61.0f);
	mCheats = InitializeSystem<CheatsSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(54 / 61.0f);
	mD20Rolls = InitializeSystem<D20RollsSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(55 / 61.0f);
	mSecretdoor = InitializeSystem<SecretdoorSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(56 / 61.0f);
	mMapFogging = InitializeSystem<MapFoggingSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(57 / 61.0f);
	mRandomEncounter = InitializeSystem<RandomEncounterSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(58 / 61.0f);
	mObjectEvent = InitializeSystem<ObjectEventSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(59 / 61.0f);
	mFormation = InitializeSystem<FormationSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(60 / 61.0f);
	mItemHighlight = InitializeSystem<ItemHighlightSystem>(loadingScreen, mConfig);
	loadingScreen.SetProgress(61 / 61.0f);
	mPathX = InitializeSystem<PathXSystem>(loadingScreen, mConfig);

}

void GameSystems::EndGame() {
	return gameSystemInitTable.EndGame();
}

void GameSystems::DestroyPlayerObject() {
	gameSystemInitTable.DestroyPlayerObject();
}

template <typename Type, typename... Args>
std::unique_ptr<Type> GameSystems::InitializeSystem(LoadingScreen& loadingScreen,
                                                    Args&&... args) {
	logger->info("Loading game system {}", Type::Name);
	msgFuncs.ProcessSystemEvents();
	loadingScreen.Render();

	auto result(std::make_unique<Type>(std::forward<Args>(args)...));
	mLoadedSystems.push_back(result.get());

	if (std::is_convertible<Type*, TimeAwareGameSystem*>()) {
		mTimeAwareSystems.push_back((TimeAwareGameSystem*)(result.get()));
	}

	if (std::is_convertible<Type*, ModuleAwareGameSystem*>()) {
		mModuleAwareSystems.push_back((ModuleAwareGameSystem*)(result.get()));
	}

	if (std::is_convertible<Type*, ResetAwareGameSystem*>()) {
		mResetAwareSystems.push_back((ResetAwareGameSystem*)(result.get()));
	}

	if (std::is_convertible<Type*, BufferResettingGameSystem*>()) {
		mBufferResettingSystems.push_back((BufferResettingGameSystem*)(result.get()));
	}

	if (std::is_convertible<Type*, SaveGameAwareGameSystem*>()) {
		mSaveGameAwareSystems.push_back((SaveGameAwareGameSystem*)(result.get()));
	}

	return std::move(result);
}

GameSystems* gameSystems = nullptr;

static class GameSystemsHooks : public TempleFix {
public:
	const char* name() override {
		return "Game System Fixes";
	}
	void apply() override {
		replaceFunction(0x10001DB0, Reset);
		replaceFunction(0x10002510, IsResetting);
	}

	static void Reset();
	static BOOL IsResetting();
} hooks;

void GameSystemsHooks::Reset() {
	gameSystems->ResetGame();
}

BOOL GameSystemsHooks::IsResetting() {
	return gameSystems->IsResetting() ? TRUE : FALSE;
}
