/****************************/
/*    BUGDOM - MAIN 		*/
/* (c)1999 Pangea Software  */
/* By Brian Greenstone      */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/


extern	Boolean			gAbortDemoFlag,gGameIsDemoFlag,gSongPlayingFlag,gDisableHiccupTimer,gGameIsRegistered;
extern	NewObjectDefinitionType	gNewObjectDefinition;
extern	float			gFramesPerSecond,gFramesPerSecondFrac,gAutoFadeStartDist;
extern	Byte		gDemoMode,gPlayerMode;
extern	WindowPtr	gCoverWindow;
extern	TQ3Point3D	gCoord;
extern	long				gMyStartX,gMyStartZ;
extern	Boolean				gDoCeiling,gDrawLensFlare;
extern	unsigned long 		gScore;
extern	ObjNode				*gPlayerObj,*gFirstNodePtr;
extern	TQ3ShaderObject		gWaterShader;
extern	short			gNumLives;
extern	short		gBestCheckPoint,gNumEnemies;
extern	u_long 		gInfobarUpdateBits;
extern	float		gCycScale,gBallTimer;
extern	signed char	gNumEnemyOfKind[];
extern	TQ3Point3D	gMyCoord;
extern	int			gMaxItemsAllocatedInAPass;
extern	GWorldPtr	gInfoBarTop;

/****************************/
/*    PROTOTYPES            */
/****************************/

static void InitArea(void);
static void CleanupLevel(void);
static void PlayArea(void);
static void DoDeathReset(void);
static void PlayGame(void);
static void CheckForCheats(void);
static void ShowDebug(void);


/****************************/
/*    CONSTANTS             */
/****************************/

#define	KILL_DELAY	4

typedef struct
{
	Byte	levelType;
	Byte	areaNum;
}LevelType;


/****************************/
/*    VARIABLES             */
/****************************/

float	gDemoVersionTimer = 0;

static LevelType	gLevelTable[NUM_LEVELS] =
					{
						LEVEL_TYPE_LAWN, 	0,			// 0: training
						LEVEL_TYPE_LAWN, 	1,			// 1: lawn
						LEVEL_TYPE_POND, 	0,			// 2: pond
						LEVEL_TYPE_FOREST, 	0,			// 3: beach
						LEVEL_TYPE_FOREST, 	1,			// 4: dragonfly attack
						LEVEL_TYPE_HIVE, 	0,			// 5: bee hive
						LEVEL_TYPE_HIVE, 	1,			// 6: queen bee
						LEVEL_TYPE_NIGHT, 	0,			// 7: night
						LEVEL_TYPE_ANTHILL,	0,			// 8: ant hill
						LEVEL_TYPE_ANTHILL,	1,			// 9: ant king
					};

u_short		gRealLevel = 0;
u_short		gLevelType = 0;
u_short		gAreaNum = 0;
u_short		gLevelTypeMask = 0;


Boolean		gShowDebug = false;
Boolean		gLiquidCheat = false;
Boolean		gUseCyclorama;
float		gCurrentYon;

u_long		gAutoFadeStatusBits;
short		gMainAppRezFile,gTextureRezfile;
Boolean		gGameOverFlag,gAbortedFlag,gAreaCompleted;
Boolean		gPlayerGotKilledFlag,gWonGameFlag,gRestoringSavedGame = false;

QD3DSetupOutputType		*gGameViewInfoPtr = nil;

PrefsType	gGamePrefs;

FSSpec		gDataSpec;


TQ3Vector3D		gLightDirection1 = { .4, -.35, 1 };		// also serves as lense flare vector
TQ3Vector3D		gLightDirection2;

TQ3ColorRGB		gAmbientColor = { 1.0, 1.0, .9 };
TQ3ColorRGB		gFillColor1 = { 1.0, 1.0, .6 };
TQ3ColorRGB		gFillColor2 = { 1.0, 1.0, 1 };


		/* LEVEL SETUP PARAMETER TABLES */
			
static const Boolean	gLevelHasCyc[NUM_LEVELS] =
{
	true,						// garden
	false,						// boat
	true,						// dragonfly
	false,						// hive
	true,						// night
	false						// anthill
};

static const Boolean	gLevelHasCeiling[NUM_LEVELS] =
{
	false,						// garden
	false,						// boat
	false,						// dragonfly
	true,						// hive
	false,						// night
	true						// anthill
};

static const Byte	gLevelSuperTileActiveRange[NUM_LEVELS] =
{
	5,						// garden
	4,						// boat
	5,						// dragonfly
	4,						// hive
	4,						// night
	4						// anthill
};

static const float	gLevelFogStart[NUM_LEVELS] =
{
	.5,						// garden
	.4,						// boat
	.6,						// forest
	.85,					// hive
	.4,						// night
	.65,					// anthill
};

static const float	gLevelFogEnd[NUM_LEVELS] =
{
	.9,						// garden
	1,						// boat
	.85,					// dragonfly
	1.0,					// hive
	.9,						// night
	1,						// anthill
};


static const float	gLevelAutoFadeStart[NUM_LEVELS] =
{
	YON_DISTANCE+400,		// garden
	0,						// boat
	0,						// dragonfly
	0,						// hive
	YON_DISTANCE-250,		// night
	0,						// anthill
};


static const Boolean	gLevelHasLenseFlare[NUM_LEVELS] =
{
	true,						// garden
	true,						// boat
	true,						// dragonfly
	false,						// hive
	true,						// night
	false						// anthill
};

static const TQ3Vector3D	gLensFlareVector[NUM_LEVELS] =
{
	.4, -.35, 1,				// garden
	.4, -.45, 1,				// boat
	.4, -.15, 1,				// dragonfly
	.4, -.35, 1,				// hive
	.4, -.35, 1,				// night
	.4, -.35, 1					// anthill
};


static const TQ3ColorRGB	gLevelLightColors[NUM_LEVELS][3] =		// 0 = ambient, 1 = fill0, 2 = fill1
{
	1.0, 1.0, .9,				// garden
	1.0, 1.0, .6,
	1.0, 1.0, 1,

	1.0, 1.0, .9,				// boat
	1.0, 1.0, .6,
	1.0, 1.0, 1,

	1.0, .6, .3,				// dragonfly
	1.0, .8, .3,
	1.0, .9, .3,

	1.0, 1.0, .8,				// hive
	1.0, 1.0, .7,
	1.0, 1.0, .9,

	.5, .5, .5,					// night
	.8, 1, .8,
	.6, .8, .7,

	.5, .5, .6,					// anthill
	.7, .7, .8,
	1.0, 1.0, 1.0
};


static const TQ3ColorARGB	gLevelFogColor[NUM_LEVELS] =
{
	1, .05,.25,.05,				// garden
	1, .9,.9,.85,				// boat
	1, 1.0,.29,.063,			// dragonfly
//	1, .9,.7,.1,				// hive
	1, .7,.6,.4,				// hive
	1, .02,.02,.08,				// night
	1, .15,.07,.15				// anthill
};



//======================================================================================
//======================================================================================
//======================================================================================


/*****************/
/* TOOLBOX INIT  */
/*****************/

void ToolBoxInit(void)
{
long		response;
FSSpec		spec;
u_long		seconds, seconds2;
int			i;
	
	gMainAppRezFile = CurResFile();

		/* FIRST VERIFY SYSTEM BEFORE GOING TOO FAR */
				
	VerifySystem();


			/* BOOT QD3D */
			
	QD3D_Boot();



		/* OPEN TEXTURES RESOURCE FILE */
				
	if (FSMakeFSSpec(gDataSpec.vRefNum, gDataSpec.parID, "\p:Sprites:Textures", &spec) != noErr)
		DoFatalAlert("\pToolBoxInit: cannot find Sprites:Textures file");
	gTextureRezfile = FSpOpenResFile(&spec, fsRdPerm);
	if (gTextureRezfile == -1)
		DoFatalAlert("\pToolBoxInit: Cannot locate Textures resource file!");
	UseResFile(gTextureRezfile);



		/* SEE IF PROCESSOR SUPPORTS frsqrte */
	
	if (!Gestalt(gestaltNativeCPUtype, &response))
	{
		switch(response)
		{
			case	gestaltCPU601:				// 601 is only that doesnt support it
					DoFatalAlert("\pSorry, but this app will not run on a PowerPC 601, only on newer Macintoshes.");
					break;
		}
	}

			/* INIT PREFERENCES */
			
	gGamePrefs.easyMode				= false;	
	gGamePrefs.playerRelativeKeys	= false;	
	gGamePrefs.reserved[0] 			= false;
	gGamePrefs.reserved[1] 			= false;
	gGamePrefs.reserved[2] 			= false;
	gGamePrefs.reserved[3] 			= false;	
	gGamePrefs.reserved[0] 			= 0;
	gGamePrefs.reserved[1] 			= 0;				
	gGamePrefs.reserved[2] 			= 0;
	gGamePrefs.reserved[3] 			= 0;				
	gGamePrefs.reserved[4] 			= 0;
	gGamePrefs.reserved[5] 			= 0;				
	gGamePrefs.reserved[6] 			= 0;				
	gGamePrefs.reserved[7] 			= 0;				
				
	LoadPrefs(&gGamePrefs);							// attempt to read from prefs file		
	
	FlushEvents ( everyEvent, REMOVE_ALL_EVENTS);



			/************************************/
            /* SEE IF GAME IS REGISTERED OR NOT */
			/************************************/

#if 0	//SHAREWARE
    CheckGameRegistration();
#else
    gGameIsRegistered = true;
#endif    

}
#pragma mark -

/******************** PLAY GAME ************************/

static void PlayGame(void)
{

			/***********************/
			/* GAME INITIALIZATION */
			/***********************/
			
	InitInventoryForGame();
	gGameOverFlag = false;


			/* CHEAT: LET USER SELECT STARTING LEVEL & AREA */

#if DEMO
	gRealLevel = 0;
#else
	UpdateInput();
	
	if (GetKeyState(KEY_F10))						// see if do level cheat
		DoLevelCheatDialog();
	else
	if (!gRestoringSavedGame)							// otherwise start @ 0 if not restoring
		gRealLevel = 0;
#endif
			/**********************/
			/* GO THRU EACH LEVEL */
			/**********************/
			//
			// Note: gRealLevel is already set if restoring a saved game
			//
					
	for (; gRealLevel < NUM_LEVELS; gRealLevel++)
	{
		if (!gGameIsRegistered)						// dont allow full access until registered
			if (gRealLevel > 2)
			{
				DoAlert("\pYou cannot play additional levels until you have registred this copy of Bugdom.");
				HideCursor();
				break;
			}
			
			/* GET LEVEL TYPE & AREA FOR THIS LEVEL */

		gLevelType = gLevelTable[gRealLevel].levelType;
		gAreaNum = gLevelTable[gRealLevel].areaNum;
		

			/* PLAY THIS AREA */
		
		ShowLevelIntroScreen();
		InitArea();

		gRestoringSavedGame = false;				// we dont need this anymore
		
		PlayArea();


			/* CLEANUP LEVEL */
					
		CleanupLevel();
		GammaFadeOut();
		GameScreenToBlack();		
		
#if DEMO
		return;
#else		
		if (gGameOverFlag)
			goto game_over;
			
		/* DO END-LEVEL BONUS SCREEN */
			
		DoBonusScreen();
#endif		
	}
	
			/*************/
			/* GAME OVER */
			/*************/
game_over:
			/* PLAY WIN MOVIE */
	
	if (gWonGameFlag)
		DoWinScreen();
	
			/* PLAY LOSE MOVIE */
	else
		DoLoseScreen();

	ShowHighScoresScreen(gScore);
}



/**************** PLAY AREA ************************/

static void PlayArea(void)
{
float killDelay = KILL_DELAY;						// time to wait after I'm dead before fading out
float fps;
	
    if (!gGameIsRegistered)                     // if not registered, then time demo
        GetDemoTimer();
	
	UpdateInput();
	QD3D_CalcFramesPerSecond();						// prime this
	QD3D_CalcFramesPerSecond();

		/* PRIME 1ST FRAME & MADE FADE EVENT */
	
	QD3D_DrawScene(gGameViewInfoPtr,DrawTerrain);
	MakeFadeEvent(true);
	

		/******************/
		/* MAIN GAME LOOP */
		/******************/

	while(true)
	{
		fps = gFramesPerSecondFrac;
		UpdateInput();

#if DEMO
		gDemoVersionTimer += fps;							// count the seconds for DEMO
#elif SHAREWARE
	    if (!gGameIsRegistered)                    			// if not registered, then time demo
			gDemoVersionTimer += fps;						// count the seconds for DEMO
#endif	

				/* SEE IF DEMO ENDED */				
		
		if (gAbortDemoFlag)
			break;
	
	
				/* SPECIFIC MAINTENANCE */

		CheckPlayerMorph();				
		UpdateLiquidAnimation();
		UpdateHoneyTubeTextureAnimation();
		UpdateRootSwings();
		
	
				/* MOVE OBJECTS */
				
		MoveObjects();
		MoveSplineObjects();
		QD3D_MoveParticles();
		MoveParticleGroups();
		UpdateCamera();
	
			/* DRAW OBJECTS & TERRAIN */
					
		UpdateInfobar();
		DoMyTerrainUpdate();
		QD3D_DrawScene(gGameViewInfoPtr,DrawTerrain);
		QD3D_CalcFramesPerSecond();
		gDisableHiccupTimer = false;


		/* SHOW DEBUG */
		
		if (gShowDebug)
			ShowDebug();
		

			/* SEE IF PAUSE GAME */
				
		if (gDemoMode != DEMO_MODE_RECORD)
		{
			if (GetNewKeyState(KEY_ESC))				// see if pause/abort
				DoPaused();
		}
		
			/* SEE IF GAME ENDED */				
		
		if (gGameOverFlag)
			break;

		if (gAreaCompleted)
		{
			if (gRealLevel == LEVEL_NUM_ANTKING)		// if completed Ant King, then I won!
				gWonGameFlag = true;
			break;
		}

			/* CHECK FOR CHEATS */
			
		CheckForCheats();
					
			
			/* SEE IF GOT KILLED */
				
		if (gPlayerGotKilledFlag)				// if got killed, then hang around for a few seconds before resetting player
		{
			killDelay -= fps;					
			if (killDelay < 0.0f)				// see if time to reset player
			{
				killDelay = KILL_DELAY;			// reset kill timer for next death
				DoDeathReset();
				if (gGameOverFlag)				// see if that's all folks
					break;
			}
		}	
	}
	
	
    if (!gGameIsRegistered)                     // if not registered, then save demo timer
        SaveDemoTimer();
			
}


/***************** INIT AREA ************************/

static void InitArea(void)
{
QD3DSetupInputType	viewDef;


			/* INIT SOME PRELIM STUFF */

	gLevelTypeMask 			= 1 << gLevelType;						// create bit mask
	gAreaCompleted 			= false;
	gPlayerMode 			= PLAYER_MODE_BUG;						// init this here so infobar looks correct
	gPlayerObj 				= nil;

	gAutoFadeStartDist	= gLevelAutoFadeStart[gLevelType];
	gUseCyclorama		 = gLevelHasCyc[gLevelType];
	gDrawLensFlare		= gLevelHasLenseFlare[gLevelType];
		
	gDoCeiling				= gLevelHasCeiling[gLevelType];
	gSuperTileActiveRange	= gLevelSuperTileActiveRange[gLevelType];
	
		
	gAmbientColor 			= gLevelLightColors[gLevelType][0];
	gFillColor1 			= gLevelLightColors[gLevelType][1];
	gFillColor2 			= gLevelLightColors[gLevelType][2];
	gLightDirection1 		= gLensFlareVector[gLevelType];		// get direction of light 0 for lense flare
	Q3Vector3D_Normalize(&gLightDirection1,&gLightDirection1);
	
	gBestCheckPoint			= -1;								// no checkpoint yet

		
	if (gSuperTileActiveRange == 5)								// set yon clipping value
	{
		gCurrentYon = YON_DISTANCE + 1700;
		gCycScale = 81;
	}
	else
	{
		gCurrentYon = YON_DISTANCE;
		gCycScale = 50;
	}



	if (gAutoFadeStartDist != 0.0f)
		gAutoFadeStatusBits = STATUS_BIT_AUTOFADE|STATUS_BIT_NOTRICACHE;
	else
		gAutoFadeStatusBits = 0;
		

			/*************/
			/* MAKE VIEW */
			/*************/

//	GameScreenToBlack();
	

			/* SETUP VIEW DEF */
			
	QD3D_NewViewDef(&viewDef, gCoverWindow);
	
	viewDef.camera.hither 			= HITHER_DISTANCE;
	viewDef.camera.yon 				= gCurrentYon;
	
	viewDef.camera.fov 				= 1.1;
	
	viewDef.view.paneClip.top		=	62;
	viewDef.view.paneClip.bottom	=	60;
	viewDef.view.paneClip.left		=	0;
	viewDef.view.paneClip.right		=	0;
		
	viewDef.view.clearColor 	= gLevelFogColor[gLevelType];	// set clear & fog color	

	
			/* SET LIGHTS */
			

	if (gDoCeiling)
	{
		viewDef.lights.numFillLights 	= 2;
		gLightDirection2.x = -.8;
		gLightDirection2.y = 1.0;
		gLightDirection2.z = -.2;
		viewDef.lights.fillBrightness[1] = .8;
	}
	else
	{
		viewDef.lights.numFillLights 	= 2;	
		gLightDirection2.x = -.2;
		gLightDirection2.y = -.7;
		gLightDirection2.z = -.1;
		viewDef.lights.fillBrightness[1] 	= .5;
	}

	Q3Vector3D_Normalize(&gLightDirection1,&gLightDirection1);
	Q3Vector3D_Normalize(&gLightDirection2,&gLightDirection2);

	viewDef.lights.ambientBrightness 	= 0.2;
	viewDef.lights.fillDirection[0] 	= gLightDirection1;
	viewDef.lights.fillDirection[1] 	= gLightDirection2;
	viewDef.lights.fillBrightness[0] 	= 1.1;
	viewDef.lights.fillBrightness[1] 	= .5;
	
	viewDef.lights.ambientColor 	= gAmbientColor;
	viewDef.lights.fillColor[0] 	= gFillColor1;
	viewDef.lights.fillColor[1] 	= gFillColor2;
	
	
			/* SET FOG */
			
	viewDef.lights.useFog 		= true;
	viewDef.lights.fogStart 	= gLevelFogStart[gLevelType];
	viewDef.lights.fogEnd	 	= gLevelFogEnd[gLevelType];
	viewDef.lights.fogDensity 	= 1.0;	
	
	viewDef.lights.fogMode = kQ3FogModeLinear;
	
//	if (gUseCyclorama && (gLevelType != LEVEL_TYPE_FOREST) && (gLevelType != LEVEL_TYPE_NIGHT))
//		viewDef.view.dontClear		= true;

		
	QD3D_SetupWindow(&viewDef, &gGameViewInfoPtr);

	
			/**********************/
			/* LOAD ART & TERRAIN */
			/**********************/
			
	LoadLevelArt();			

	
				/* INIT FLAGS */
				
	gAbortDemoFlag = gGameOverFlag = false;
	gPlayerGotKilledFlag = false;
	gWonGameFlag = false;

		/* DRAW INITIAL INFOBAR */
				
	InitInventoryForArea();					// must call after terrain is loaded!!
	InitInfobar();			
	

			/* INIT OTHER MANAGERS */

	CreateSuperTileMemoryList();	
	
	QD3D_InitParticles();	
	InitParticleSystem();
	InitItemsManager();

		
		/* INIT THE PLAYER */
			
	InitPlayerAtStartOfLevel();
	InitEnemyManager();	
						
			/* INIT CAMERA */
			
	InitCamera();
	
			/* PREP THE TERRAIN */
			
	PrimeInitialTerrain(false);

			/* INIT BACKGROUND */
			
	if (gUseCyclorama)
		CreateCyclorama();
		
	HideCursor();								// do this again to be sure!
 }


/**************** CLEANUP LEVEL **********************/

static void CleanupLevel(void)
{
	StopAllEffectChannels();
	StopDemo();
	QD3D_DisposeWindowSetup(&gGameViewInfoPtr);
 	EmptySplineObjectList();
	DeleteAllObjects();
	FreeAllSkeletonFiles(-1);
	DisposeSuperTileMemoryList();
	DisposeTerrain();
	DeleteAllParticleGroups();
	DisposeFenceShaders();
	DisposeLensFlares();
	DisposeLiquids();
	DeleteAll3DMFGroups();
	
	if (gInfoBarTop)								// dispose of infobar cached image
	{
		DisposeGWorld(gInfoBarTop);
		gInfoBarTop = nil;
	}
	
}




/************************* DO DEATH RESET ******************************/
//
// Called from above when player is killed.
//

static void DoDeathReset(void)
{
	GammaFadeOut();												// fade out

			/* SEE IF THAT WAS THE LAST LIFE */
			
	gNumLives--;
	gInfobarUpdateBits |= UPDATE_LIVES;
	if (gNumLives <= 0)
	{
		gGameOverFlag = true;
		return;
	}
	

			/* RESET THE PLAYER & CAMERA INFO */
				
	ResetPlayer();
	InitCamera();
	
	
		/* RESET TERRAIN SCROLL AT LAST CHECKPOINT */
		
	InitCurrentScrollSettings();
	PrimeInitialTerrain(true);
	
	MakeFadeEvent(true);
}



/******************** CHECK FOR CHEATS ************************/

static void CheckForCheats(void)
{
	if (GetKeyState(KEY_APPLE))				// must hold down the help key
	{
		if (GetNewKeyState(KEY_F1))			// win the level!
			gAreaCompleted = true;
			
		if (GetNewKeyState(KEY_F2))			// win the game!
		{
			gGameOverFlag = true;
			gWonGameFlag = true;
		}
		
		if (GetNewKeyState(KEY_F3))			// get full health
			GetHealth(1.0);							
			
		if (GetNewKeyState(KEY_F4))			// get full ball-time
		{
			gBallTimer = 1.0f;
			gInfobarUpdateBits |= UPDATE_TIMER;	
		}	
		
		if (GetNewKeyState(KEY_F5))			// get full inventory
		{
			GetMoney();
			GetKey(0);
			GetKey(1);
			GetKey(2);
			GetKey(3);
			GetKey(4);
		}	
		
		if (GetNewKeyState(KEY_F9))
			gShowDebug = !gShowDebug;


		if (GetNewKeyState(KEY_F6))			// see if liquid invincible
			gLiquidCheat = !gLiquidCheat;
		
	}
}

#pragma mark -

/********************* SHOW DEBUG ***********************/

static void ShowDebug(void)
{
}




#pragma mark -

/************************************************************/
/******************** PROGRAM MAIN ENTRY  *******************/
/************************************************************/


int main(void)
{
unsigned long	someLong;

				/**************/
				/* BOOT STUFF */
				/**************/
				
	ToolBoxInit();
 	


			/* INIT SOME OF MY STUFF */

	InitWindowStuff();
	InitTerrainManager();
	InitSkeletonManager();
	InitSoundTools();
	Init3DMFManager();	
	InitFenceManager();
	InitParticleSystem();


			/* INIT MORE MY STUFF */
					
	InitObjectManager();
	LoadInfobarArt();
	
	GetDateTime ((unsigned long *)(&someLong));		// init random seed
	SetMyRandomSeed(someLong);
	HideCursor();

			/* SEE IF DEMO VERSION EXPIRED */
			
#if DEMO
	GetDemoTimer();
#endif	



			/* DO INTRO */
			
	ShowIntroScreens();
	DoPangeaLogo();


		/* MAIN LOOP */
			
	while(true)
	{
		DoTitleScreen();
		if (DoMainMenu())
			continue;
		
		PlayGame();
	}
	
	
	return(0);
}



