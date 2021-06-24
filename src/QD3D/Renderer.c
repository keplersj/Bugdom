// RENDERER.C
// (C)2021 Iliyas Jorio
// This file is part of Bugdom. https://github.com/jorio/bugdom


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <QD3D.h>
#include <stdlib.h>		// qsort


#pragma mark -

/****************************/
/*    PROTOTYPES            */
/****************************/

enum
{
	kRenderPass_Depth,
	kRenderPass_Opaque,
	kRenderPass_Transparent
};

typedef struct RendererState
{
	GLuint		boundTexture;
	bool		hasClientState_GL_TEXTURE_COORD_ARRAY;
	bool		hasClientState_GL_VERTEX_ARRAY;
	bool		hasClientState_GL_COLOR_ARRAY;
	bool		hasClientState_GL_NORMAL_ARRAY;
	bool		hasState_GL_CULL_FACE;
	bool		hasState_GL_ALPHA_TEST;
	bool		hasState_GL_DEPTH_TEST;
	bool		hasState_GL_SCISSOR_TEST;
	bool		hasState_GL_COLOR_MATERIAL;
	bool		hasState_GL_TEXTURE_2D;
	bool		hasState_GL_BLEND;
	bool		hasState_GL_LIGHTING;
	bool		hasState_GL_FOG;
	bool		hasFlag_glDepthMask;
	bool		blendFuncIsAdditive;
	bool		wantFog;
} RendererState;

typedef struct MeshQueueEntry
{
	int						numMeshes;
	TQ3TriMeshData**		meshPtrList;
	TQ3TriMeshData*			mesh0;
	const TQ3Matrix4x4*		transform;
	const RenderModifiers*	mods;
	float					depthSortZ;
} MeshQueueEntry;

#define MESHQUEUE_MAX_SIZE 4096

static MeshQueueEntry		gMeshQueueBuffer[MESHQUEUE_MAX_SIZE];
static MeshQueueEntry*		gMeshQueuePtrs[MESHQUEUE_MAX_SIZE];
static int					gMeshQueueSize = 0;
static bool					gFrameStarted = false;

static float				gBackupVertexColors[4*65536];

static int DepthSortCompare(void const* a_void, void const* b_void);
static void DrawFadeOverlay(float opacity);

static void DrawMeshes(
		int renderPass,
		const MeshQueueEntry* entry,
		bool (*preDrawFunc)(int renderPass, const MeshQueueEntry* entry, int nthMesh)
);
static bool PreDrawMesh_DepthPass(int renderPass, const MeshQueueEntry* entry, int nthMesh);
static bool PreDrawMesh_ColorPass(int renderPass, const MeshQueueEntry* entry, int nthMesh);


#pragma mark -

/****************************/
/*    CONSTANTS             */
/****************************/

static const RenderModifiers kDefaultRenderMods =
{
	.statusBits = 0,
	.diffuseColor = {1,1,1,1},
	.autoFadeFactor = 1.0f,
	.sortPriority = 0,
};

static const float kFreezeFrameFadeOutDuration = .33f;

//		2----3
//		| \  |
//		|  \ |
//		0----1
static const TQ3Point2D kFullscreenQuadPointsNDC[4] =
{
	{-1.0f, -1.0f},
	{ 1.0f, -1.0f},
	{-1.0f,  1.0f},
	{ 1.0f,  1.0f},
};

static const uint8_t kFullscreenQuadTriangles[2][3] =
{
	{0, 1, 2},
	{1, 3, 2},
};

static const TQ3Param2D kFullscreenQuadUVs[4] =
{
	{0, 1},
	{1, 1},
	{0, 0},
	{1, 0},
};

static const TQ3Param2D kFullscreenQuadUVsFlipped[4] =
{
	{0, 0},
	{1, 0},
	{0, 1},
	{1, 1},
};


#pragma mark -

/****************************/
/*    VARIABLES             */
/****************************/

static RendererState gState;

static PFNGLDRAWRANGEELEMENTSPROC __glDrawRangeElements;

static	float			gFadeOverlayOpacity = 0;

#pragma mark -

/****************************/
/*    MACROS/HELPERS        */
/****************************/

static void Render_GetGLProcAddresses(void)
{
	__glDrawRangeElements = (PFNGLDRAWRANGEELEMENTSPROC)SDL_GL_GetProcAddress("glDrawRangeElements");  // missing link with something...?
	GAME_ASSERT(__glDrawRangeElements);
}

static void __SetInitialState(GLenum stateEnum, bool* stateFlagPtr, bool initialValue)
{
	*stateFlagPtr = initialValue;
	if (initialValue)
		glEnable(stateEnum);
	else
		glDisable(stateEnum);
	CHECK_GL_ERROR();
}

static void __SetInitialClientState(GLenum stateEnum, bool* stateFlagPtr, bool initialValue)
{
	*stateFlagPtr = initialValue;
	if (initialValue)
		glEnableClientState(stateEnum);
	else
		glDisableClientState(stateEnum);
	CHECK_GL_ERROR();
}

static inline void __SetState(GLenum stateEnum, bool* stateFlagPtr, bool enable)
{
	if (enable != *stateFlagPtr)
	{
		if (enable)
			glEnable(stateEnum);
		else
			glDisable(stateEnum);
		*stateFlagPtr = enable;
	}
	else
		gRenderStats.batchedStateChanges++;
}

static inline void __SetClientState(GLenum stateEnum, bool* stateFlagPtr, bool enable)
{
	if (enable != *stateFlagPtr)
	{
		if (enable)
			glEnableClientState(stateEnum);
		else
			glDisableClientState(stateEnum);
		*stateFlagPtr = enable;
	}
	else
		gRenderStats.batchedStateChanges++;
}

#define SetInitialState(stateEnum, initialValue) __SetInitialState(stateEnum, &gState.hasState_##stateEnum, initialValue)
#define SetInitialClientState(stateEnum, initialValue) __SetInitialClientState(stateEnum, &gState.hasClientState_##stateEnum, initialValue)

#define SetState(stateEnum, value) __SetState(stateEnum, &gState.hasState_##stateEnum, (value))

#define EnableState(stateEnum) __SetState(stateEnum, &gState.hasState_##stateEnum, true)
#define EnableClientState(stateEnum) __SetClientState(stateEnum, &gState.hasClientState_##stateEnum, true)

#define DisableState(stateEnum) __SetState(stateEnum, &gState.hasState_##stateEnum, false)
#define DisableClientState(stateEnum) __SetClientState(stateEnum, &gState.hasClientState_##stateEnum, false)

#define RestoreStateFromBackup(stateEnum, backup) __SetState(stateEnum, &gState.hasState_##stateEnum, (backup)->hasState_##stateEnum)
#define RestoreClientStateFromBackup(stateEnum, backup) __SetClientState(stateEnum, &gState.hasClientState_##stateEnum, (backup)->hasClientState_##stateEnum)

#define SetFlag(glFunction, value) do {				\
	if ((value) != gState.hasFlag_##glFunction) {	\
		glFunction((value)? GL_TRUE: GL_FALSE);		\
		gState.hasFlag_##glFunction = (value);		\
	} } while(0)


#pragma mark -

//=======================================================================================================

/****************************/
/*    API IMPLEMENTATION    */
/****************************/

#if _DEBUG
void DoFatalGLError(GLenum error, const char* file, int line)
{
	static char alertbuf[1024];
	snprintf(alertbuf, sizeof(alertbuf), "OpenGL error 0x%x: %s\nin %s:%d",
		error,
		(const char*)gluErrorString(error),
		file,
		line);
	DoFatalAlert(alertbuf);
}
#endif

void Render_SetDefaultModifiers(RenderModifiers* dest)
{
	memcpy(dest, &kDefaultRenderMods, sizeof(RenderModifiers));
}

void Render_InitState(void)
{
	// On Windows, proc addresses are only valid for the current context, so we must get fetch everytime we recreate the context.
	Render_GetGLProcAddresses();

	SetInitialClientState(GL_VERTEX_ARRAY,				true);
	SetInitialClientState(GL_NORMAL_ARRAY,				true);
	SetInitialClientState(GL_COLOR_ARRAY,				false);
	SetInitialClientState(GL_TEXTURE_COORD_ARRAY,		true);
	SetInitialState(GL_CULL_FACE,		true);
	SetInitialState(GL_ALPHA_TEST,		true);
	SetInitialState(GL_DEPTH_TEST,		true);
	SetInitialState(GL_SCISSOR_TEST,	false);
	SetInitialState(GL_COLOR_MATERIAL,	true);
	SetInitialState(GL_TEXTURE_2D,		false);
	SetInitialState(GL_BLEND,			false);
	SetInitialState(GL_LIGHTING,		true);
	SetInitialState(GL_FOG,				false);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gState.blendFuncIsAdditive = false;

	gState.hasFlag_glDepthMask = true;		// initially active on a fresh context

	gState.boundTexture = 0;
	gState.wantFog = false;

	gMeshQueueSize = 0;
	for (int i = 0; i < MESHQUEUE_MAX_SIZE; i++)
		gMeshQueuePtrs[i] = &gMeshQueueBuffer[i];
}

void Render_EnableFog(
		float camHither,
		float camYon,
		float fogHither,
		float fogYon,
		TQ3ColorRGBA fogColor)
{
	glHint(GL_FOG_HINT,		GL_NICEST);
	glFogi(GL_FOG_MODE,		GL_LINEAR);
	glFogf(GL_FOG_START,	fogHither * camYon);
	glFogf(GL_FOG_END,		fogYon * camYon);
	glFogfv(GL_FOG_COLOR,	(float *)&fogColor);
	gState.wantFog = true;
}

void Render_DisableFog(void)
{
	gState.wantFog = false;
}

#pragma mark -

void Render_BindTexture(GLuint textureName)
{
	if (gState.boundTexture != textureName)
	{
		glBindTexture(GL_TEXTURE_2D, textureName);
		gState.boundTexture = textureName;
	}
	else
	{
		gRenderStats.batchedStateChanges++;
	}
}

GLuint Render_LoadTexture(
		GLenum internalFormat,
		int width,
		int height,
		GLenum bufferFormat,
		GLenum bufferType,
		const GLvoid* pixels,
		RendererTextureFlags flags)
{
	GAME_ASSERT(gGLContext);

	GLuint textureName;

	glGenTextures(1, &textureName);
	CHECK_GL_ERROR();

	Render_BindTexture(textureName);				// this is now the currently active texture
	CHECK_GL_ERROR();

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gGamePrefs.textureFiltering? GL_LINEAR: GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gGamePrefs.textureFiltering? GL_LINEAR: GL_NEAREST);

	if (flags & kRendererTextureFlags_ClampU)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

	if (flags & kRendererTextureFlags_ClampV)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(
			GL_TEXTURE_2D,
			0,						// mipmap level
			internalFormat,			// format in OpenGL
			width,					// width in pixels
			height,					// height in pixels
			0,						// border
			bufferFormat,			// what my format is
			bufferType,				// size of each r,g,b
			pixels);				// pointer to the actual texture pixels
	CHECK_GL_ERROR();

	return textureName;
}

void Render_UpdateTexture(
		GLuint textureName,
		int x,
		int y,
		int width,
		int height,
		GLenum bufferFormat,
		GLenum bufferType,
		const GLvoid* pixels,
		int rowBytesInInput)
{
	GLint pUnpackRowLength = 0;

	Render_BindTexture(textureName);

	// Set unpack row length (if valid rowbytes input given)
	if (rowBytesInInput > 0)
	{
		glGetIntegerv(GL_UNPACK_ROW_LENGTH, &pUnpackRowLength);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, rowBytesInInput);
	}

	glTexSubImage2D(
			GL_TEXTURE_2D,
			0,
			x,
			y,
			width,
			height,
			bufferFormat,
			bufferType,
			pixels);
	CHECK_GL_ERROR();

	// Restore unpack row length
	if (rowBytesInInput > 0)
	{
		glPixelStorei(GL_UNPACK_ROW_LENGTH, pUnpackRowLength);
	}
}

void Render_Load3DMFTextures(TQ3MetaFile* metaFile, GLuint* outTextureNames, bool forceClampUVs)
{
	for (int i = 0; i < metaFile->numTextures; i++)
	{
		TQ3TextureShader* textureShader = &metaFile->textures[i];

		GAME_ASSERT(textureShader->pixmap);

		TQ3TexturingMode meshTexturingMode = kQ3TexturingModeOff;
		GLenum internalFormat;
		GLenum format;
		GLenum type;
		switch (textureShader->pixmap->pixelType)
		{
			case kQ3PixelTypeRGB32:
				meshTexturingMode = kQ3TexturingModeOpaque;
				internalFormat = GL_RGB;
				format = GL_BGRA;
				type = GL_UNSIGNED_INT_8_8_8_8_REV;
				break;
			case kQ3PixelTypeARGB32:
				meshTexturingMode = kQ3TexturingModeAlphaBlend;
				internalFormat = GL_RGBA;
				format = GL_BGRA;
				type = GL_UNSIGNED_INT_8_8_8_8_REV;
				break;
			case kQ3PixelTypeRGB16:
				meshTexturingMode = kQ3TexturingModeOpaque;
				internalFormat = GL_RGB;
				format = GL_BGRA;
				type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
				break;
			case kQ3PixelTypeARGB16:
				meshTexturingMode = kQ3TexturingModeAlphaTest;
				internalFormat = GL_RGBA;
				format = GL_BGRA;
				type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
				break;
			case kQ3PixelTypeRGB24:
				meshTexturingMode = kQ3TexturingModeOpaque;
				internalFormat = GL_RGB;
				format = GL_BGR;
				type = GL_UNSIGNED_BYTE;
				break;
			default:
				DoAlert("3DMF texture: Unsupported kQ3PixelType");
				continue;
		}

		int clampFlags = forceClampUVs ? kRendererTextureFlags_ClampBoth : 0;
		if (textureShader->boundaryU == kQ3ShaderUVBoundaryClamp)
			clampFlags |= kRendererTextureFlags_ClampU;
		if (textureShader->boundaryV == kQ3ShaderUVBoundaryClamp)
			clampFlags |= kRendererTextureFlags_ClampV;

		outTextureNames[i] = Render_LoadTexture(
					 internalFormat,						// format in OpenGL
					 textureShader->pixmap->width,			// width in pixels
					 textureShader->pixmap->height,			// height in pixels
					 format,								// what my format is
					 type,									// size of each r,g,b
					 textureShader->pixmap->image,			// pointer to the actual texture pixels
					 clampFlags);

		// Set glTextureName on meshes
		for (int j = 0; j < metaFile->numMeshes; j++)
		{
			if (metaFile->meshes[j]->internalTextureID == i)
			{
				metaFile->meshes[j]->glTextureName = outTextureNames[i];
				metaFile->meshes[j]->texturingMode = meshTexturingMode;
			}
		}
	}
}

#pragma mark -

void Render_StartFrame(void)
{
	// Clear rendering statistics
	memset(&gRenderStats, 0, sizeof(gRenderStats));

	// Clear transparent queue
	gMeshQueueSize = 0;
	gRenderStats.meshQueueSize = 0;

	// Clear color & depth buffers.
	SetFlag(glDepthMask, true);	// The depth mask must be re-enabled so we can clear the depth buffer.
	glDepthMask(true);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

//	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	GAME_ASSERT(!gFrameStarted);
	gFrameStarted = true;
}

void Render_SetViewport(bool scissor, int x, int y, int w, int h)
{
	if (scissor)
	{
		EnableState(GL_SCISSOR_TEST);
		glScissor	(x,y,w,h);
		glViewport	(x,y,w,h);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	else
	{
		glViewport	(x,y,w,h);
	}
}

void Render_FlushQueue(void)
{
	GAME_ASSERT(gFrameStarted);

	// Keep track of transparent queue size for debug stats
	gRenderStats.meshQueueSize += gMeshQueueSize;

	// Flush mesh draw queue
	if (gMeshQueueSize == 0)
		return;

	// Sort mesh draw queue, front to back
	qsort(
			gMeshQueuePtrs,
			gMeshQueueSize,
			sizeof(gMeshQueuePtrs[0]),
			DepthSortCompare
	);

	// PASS 0: build depth buffer
	glColor4f(1,1,1,1);
	glDepthMask(true);
	glDepthFunc(GL_LESS);
	glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
	DisableClientState(GL_COLOR_ARRAY);
	DisableClientState(GL_NORMAL_ARRAY);
	EnableState(GL_DEPTH_TEST);
	for (int i = 0; i < gMeshQueueSize; i++)
	{
		DrawMeshes(kRenderPass_Depth, gMeshQueuePtrs[i], PreDrawMesh_DepthPass);
	}

	// PASS 1: draw opaque meshes, front to back
	glDepthMask(false);
	glDepthFunc(GL_LEQUAL);
	glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
	for (int i = 0; i < gMeshQueueSize; i++)
	{
		DrawMeshes(kRenderPass_Opaque, gMeshQueuePtrs[i], PreDrawMesh_ColorPass);
	}

	// PASS 2: draw transparent meshes, back to front
	for (int i = gMeshQueueSize-1; i >= 0; i--)
	{
		DrawMeshes(kRenderPass_Transparent, gMeshQueuePtrs[i], PreDrawMesh_ColorPass);
	}

	// Clear mesh draw queue
	gMeshQueueSize = 0;
}

void Render_EndFrame(void)
{
	GAME_ASSERT(gFrameStarted);

	Render_FlushQueue();

	DisableState(GL_SCISSOR_TEST);

#if ALLOW_FADE
	// Draw fade overlay
	if (gFadeOverlayOpacity > 0.01f)
	{
		DrawFadeOverlay(gFadeOverlayOpacity);
	}
#endif

	gFrameStarted = false;
}

#pragma mark -

static float GetDepthSortZ(
		int						numMeshes,
		TQ3TriMeshData**		meshList,
		const TQ3Point3D*		centerCoord)
{
	TQ3Point3D altCenter;

	if (!centerCoord)
	{
		float mult = (float) numMeshes / 2.0f;
		altCenter = (TQ3Point3D) { 0, 0, 0 };
		for (int i = 0; i < numMeshes; i++)
		{
			altCenter.x += (meshList[i]->bBox.min.x + meshList[i]->bBox.max.x) * mult;
			altCenter.y += (meshList[i]->bBox.min.y + meshList[i]->bBox.max.y) * mult;
			altCenter.z += (meshList[i]->bBox.min.z + meshList[i]->bBox.max.z) * mult;
		}
		centerCoord = &altCenter;
	}

	TQ3Point3D coordInFrustum;
	Q3Point3D_Transform(centerCoord, &gCameraWorldToFrustumMatrix, &coordInFrustum);

	return coordInFrustum.z;
}

void Render_SubmitMeshList(
		int						numMeshes,
		TQ3TriMeshData**		meshList,
		const TQ3Matrix4x4*		transform,
		const RenderModifiers*	mods,
		const TQ3Point3D*		centerCoord)
{
	if (numMeshes <= 0)
		printf("not drawing this!\n");

	GAME_ASSERT(gFrameStarted);
	GAME_ASSERT(gMeshQueueSize < MESHQUEUE_MAX_SIZE);

	MeshQueueEntry* entry = gMeshQueuePtrs[gMeshQueueSize++];
	entry->numMeshes		= numMeshes;
	entry->meshPtrList		= meshList;
	entry->mesh0			= nil;
	entry->transform		= transform;
	entry->mods				= mods ? mods : &kDefaultRenderMods;
	entry->depthSortZ		= GetDepthSortZ(numMeshes, meshList, centerCoord);
}

void Render_SubmitMesh(
		TQ3TriMeshData*			mesh,
		const TQ3Matrix4x4*		transform,
		const RenderModifiers*	mods,
		const TQ3Point3D*		centerCoord)
{
	GAME_ASSERT(gFrameStarted);
	GAME_ASSERT(gMeshQueueSize < MESHQUEUE_MAX_SIZE);

	MeshQueueEntry* entry = gMeshQueuePtrs[gMeshQueueSize++];
	entry->numMeshes		= 1;
	entry->meshPtrList		= &entry->mesh0;
	entry->mesh0			= mesh;
	entry->transform		= transform;
	entry->mods				= mods ? mods : &kDefaultRenderMods;
	entry->depthSortZ		= GetDepthSortZ(1, &mesh, centerCoord);
}

#pragma mark -

static int DepthSortCompare(void const* a_void, void const* b_void)
{
	static const int AFirst		= -1;
	static const int BFirst		= +1;
	static const int DontCare	= 0;

	const MeshQueueEntry* a = *(MeshQueueEntry**) a_void;
	const MeshQueueEntry* b = *(MeshQueueEntry**) b_void;

	// First check manual priority
	if (a->mods->sortPriority < b->mods->sortPriority)
		return AFirst;

	if (a->mods->sortPriority > b->mods->sortPriority)
		return BFirst;

	// Next, if both A and B have the same manual priority, compare their depth coord
	if (a->depthSortZ < b->depthSortZ)		// A is closer to the camera
		return AFirst;

	if (a->depthSortZ > b->depthSortZ)		// B is closer to the camera
		return BFirst;

	return DontCare;
}

#pragma mark -

static void DrawMeshes(
		int renderPass,
		const MeshQueueEntry* entry,
		bool (*preDrawFunc)(int renderPass, const MeshQueueEntry* entry, int nthMesh))
{
	bool matrixPushedYet = false;
	uint32_t statusBits = entry->mods->statusBits;

	for (int i = 0; i < entry->numMeshes; i++)
	{
		const TQ3TriMeshData* mesh = entry->meshPtrList[i];

		// Skip if hidden
		if (statusBits & STATUS_BIT_HIDDEN)
			continue;

		// Call the preDraw function for this pass
		if (!preDrawFunc(renderPass, entry, i))
			continue;

		// Cull backfaces or not
		SetState(GL_CULL_FACE, !(statusBits & STATUS_BIT_KEEPBACKFACES));

		// To keep backfaces on a transparent mesh, draw backfaces first, then frontfaces.
		// This enhances the appearance of e.g. translucent spheres,
		// without the need to depth-sort individual faces.
		if (statusBits & STATUS_BIT_KEEPBACKFACES_2PASS)
			glCullFace(GL_FRONT);		// Pass 1: draw backfaces (cull frontfaces)

		// Submit vertex data
		glVertexPointer(3, GL_FLOAT, 0, mesh->points);

		// Submit transformation matrix if any
		if (!matrixPushedYet && entry->transform)
		{
			glPushMatrix();
			glMultMatrixf((float*)entry->transform->value);
			matrixPushedYet = true;
		}

		// Draw the mesh
		__glDrawRangeElements(GL_TRIANGLES, 0, mesh->numPoints-1, mesh->numTriangles*3, GL_UNSIGNED_SHORT, mesh->triangles);
		CHECK_GL_ERROR();

		// Pass 2 to draw transparent meshes without face culling (see above for an explanation)
		if (statusBits & STATUS_BIT_KEEPBACKFACES_2PASS)
		{
			// Restored glCullFace to GL_BACK, which is the default for all other meshes.
			glCullFace(GL_BACK);	// pass 2: draw frontfaces (cull backfaces)

			// Draw the mesh again
			__glDrawRangeElements(GL_TRIANGLES, 0, mesh->numPoints - 1, mesh->numTriangles * 3, GL_UNSIGNED_SHORT, mesh->triangles);
			CHECK_GL_ERROR();
		}

		// Update stats
		gRenderStats.trianglesDrawn += mesh->numTriangles;
	}

	if (matrixPushedYet)
	{
		glPopMatrix();
	}
}

static bool PreDrawMesh_DepthPass(int renderPass, const MeshQueueEntry* entry, int nthMesh)
{
	const TQ3TriMeshData* mesh = entry->meshPtrList[nthMesh];
	uint32_t statusBits = entry->mods->statusBits;

	if (statusBits & STATUS_BIT_NOZWRITE)
		return false;

	// Texture mapping
	if (mesh->texturingMode == kQ3TexturingModeAlphaTest ||
		mesh->texturingMode == kQ3TexturingModeAlphaBlend)
	{
		GAME_ASSERT(mesh->vertexUVs);

		EnableState(GL_ALPHA_TEST);
		EnableState(GL_TEXTURE_2D);
		EnableClientState(GL_TEXTURE_COORD_ARRAY);
		Render_BindTexture(mesh->glTextureName);

		if (statusBits & STATUS_BIT_CLAMP_U)
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		if (statusBits & STATUS_BIT_CLAMP_V)
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexCoordPointer(2, GL_FLOAT, 0, mesh->vertexUVs);
		CHECK_GL_ERROR();
	}
	else
	{
		DisableState(GL_ALPHA_TEST);
		DisableState(GL_TEXTURE_2D);
		DisableClientState(GL_TEXTURE_COORD_ARRAY);
		CHECK_GL_ERROR();
	}

	return true;
}

static bool PreDrawMesh_ColorPass(int renderPass, const MeshQueueEntry* entry, int nthMesh)
{
	const TQ3TriMeshData* mesh = entry->meshPtrList[nthMesh];
	uint32_t statusBits = entry->mods->statusBits;

	bool meshIsTransparent = mesh->texturingMode == kQ3TexturingModeAlphaBlend
			|| mesh->diffuseColor.a < .999f
			|| entry->mods->diffuseColor.a < .999f
			|| (statusBits & STATUS_BIT_GLOW)
			|| entry->mods->autoFadeFactor < .999f
			;

	// Decide whether or not to draw this mesh in this pass, depending on which pass we're in
	// (opaque or transparent), and whether the mesh has transparency.
	if ((renderPass == kRenderPass_Opaque		&& meshIsTransparent) ||
		(renderPass == kRenderPass_Transparent	&& !meshIsTransparent))
	{
		// Skip this mesh in this pass
		return false;
	}

	// Enable alpha blending if the mesh has transparency
	SetState(GL_BLEND, meshIsTransparent);

	// Set blending function for transparent meshes
	if (meshIsTransparent)
	{
		bool wantAdditive = !!(entry->mods->statusBits & STATUS_BIT_GLOW);
		if (gState.blendFuncIsAdditive != wantAdditive)
		{
			if (wantAdditive)
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			else
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			gState.blendFuncIsAdditive = wantAdditive;
		}
	}

	// Enable alpha testing if the mesh's texture calls for it
	SetState(GL_ALPHA_TEST, !meshIsTransparent && mesh->texturingMode == kQ3TexturingModeAlphaTest);

	// Environment map effect
	if (statusBits & STATUS_BIT_REFLECTIONMAP)
		EnvironmentMapTriMesh(mesh, entry->transform);

	// Apply gouraud or null illumination
	SetState(GL_LIGHTING, !(statusBits & STATUS_BIT_NULLSHADER));

	// Apply fog or not
	SetState(GL_FOG, gState.wantFog && !(statusBits & STATUS_BIT_NOFOG));

	// Texture mapping
	if (mesh->texturingMode != kQ3TexturingModeOff)
	{
		EnableState(GL_TEXTURE_2D);
		EnableClientState(GL_TEXTURE_COORD_ARRAY);
		Render_BindTexture(mesh->glTextureName);

		if (statusBits & STATUS_BIT_CLAMP_U)
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		if (statusBits & STATUS_BIT_CLAMP_V)
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexCoordPointer(2, GL_FLOAT, 0,
				statusBits & STATUS_BIT_REFLECTIONMAP ? gEnvMapUVs : mesh->vertexUVs);
		CHECK_GL_ERROR();
	}
	else
	{
		DisableState(GL_TEXTURE_2D);
		DisableClientState(GL_TEXTURE_COORD_ARRAY);
		CHECK_GL_ERROR();
	}

	// Per-vertex colors (unused in Nanosaur, will be in Bugdom)
	if (mesh->hasVertexColors)
	{
		EnableClientState(GL_COLOR_ARRAY);

		if (entry->mods->autoFadeFactor < .999f)
		{
			// Old-school OpenGL will ignore the diffuse color (used for transparency) if we also send
			// per-vertex colors. So, apply transparency to the per-vertex color array.
			GAME_ASSERT(4 * mesh->numPoints <= (int)(sizeof(gBackupVertexColors) / sizeof(gBackupVertexColors[0])));
			int j = 0;
			for (int v = 0; v < mesh->numPoints; v++)
			{
				gBackupVertexColors[j++] = mesh->vertexColors[v].r;
				gBackupVertexColors[j++] = mesh->vertexColors[v].g;
				gBackupVertexColors[j++] = mesh->vertexColors[v].b;
				gBackupVertexColors[j++] = mesh->vertexColors[v].a * entry->mods->autoFadeFactor;
			}
			glColorPointer(4, GL_FLOAT, 0, gBackupVertexColors);
		}
		else
		{
			glColorPointer(4, GL_FLOAT, 0, mesh->vertexColors);
		}
	}
	else
	{
		DisableClientState(GL_COLOR_ARRAY);

		// Apply diffuse color for the entire mesh
		glColor4f(
			mesh->diffuseColor.r * entry->mods->diffuseColor.r,
			mesh->diffuseColor.g * entry->mods->diffuseColor.g,
			mesh->diffuseColor.b * entry->mods->diffuseColor.b,
			mesh->diffuseColor.a * entry->mods->diffuseColor.a * entry->mods->autoFadeFactor
		);
	}

	// Submit normal data if any
	if (mesh->hasVertexNormals && !(statusBits & STATUS_BIT_NULLSHADER))
	{
		EnableClientState(GL_NORMAL_ARRAY);
		glNormalPointer(GL_FLOAT, 0, mesh->vertexNormals);
	}
	else
	{
		DisableClientState(GL_NORMAL_ARRAY);
	}

	return true;
}

#pragma mark -

//=======================================================================================================

/****************************/
/*    2D    */
/****************************/

static void Render_EnterExit2D_Full640x480(bool enter)
{
	static RendererState backup3DState;

	if (enter)
	{
		backup3DState = gState;
		glViewport(0, 0, gWindowWidth, gWindowHeight);
		DisableState(GL_SCISSOR_TEST);

		DisableState(GL_LIGHTING);
		DisableState(GL_FOG);
		DisableState(GL_DEPTH_TEST);
		DisableState(GL_ALPHA_TEST);
		EnableState(GL_BLEND);
//		DisableState(GL_TEXTURE_2D);
//		DisableClientState(GL_TEXTURE_COORD_ARRAY);
		DisableClientState(GL_COLOR_ARRAY);
		DisableClientState(GL_NORMAL_ARRAY);
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		//glOrtho(-1, 1,  -1, 1, 0, 1000);
		glOrtho(0,640,480,0,0,1000);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
	{
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		RestoreStateFromBackup(GL_SCISSOR_TEST,	&backup3DState);
		RestoreStateFromBackup(GL_LIGHTING,		&backup3DState);
		RestoreStateFromBackup(GL_FOG,			&backup3DState);
		RestoreStateFromBackup(GL_DEPTH_TEST,	&backup3DState);
		RestoreStateFromBackup(GL_ALPHA_TEST,	&backup3DState);
		RestoreStateFromBackup(GL_BLEND,		&backup3DState);
//		RestoreStateFromBackup(GL_TEXTURE_2D,	&backup3DState);
//		RestoreClientStateFromBackup(GL_TEXTURE_COORD_ARRAY, &backup3DState);
		RestoreClientStateFromBackup(GL_COLOR_ARRAY,	&backup3DState);
		RestoreClientStateFromBackup(GL_NORMAL_ARRAY,	&backup3DState);

		if (backup3DState.blendFuncIsAdditive)
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	}
}

void Render_Enter2D_Full640x480(void)
{
	Render_EnterExit2D_Full640x480(true);
}

void Render_Exit2D_Full640x480(void)
{
	Render_EnterExit2D_Full640x480(false);
}

void Render_Enter2D_NormalizedCoordinates(float aspect)
{
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(-aspect, aspect, -1, 1, 0, 1000);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
}

void Render_Exit2D_NormalizedCoordinates(void)
{
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

void Render_Draw2DQuad(
		int texture)
{
	float screenLeft   = 0.0f;
	float screenRight  = (float)gWindowWidth;
	float screenTop    = 0.0f;
	float screenBottom = (float)gWindowHeight;



	// Compute normalized device coordinates for the quad vertices.
	float ndcLeft   = 2.0f * screenLeft  / gWindowWidth - 1.0f;
	float ndcRight  = 2.0f * screenRight / gWindowWidth - 1.0f;
	float ndcTop    = 1.0f - 2.0f * screenTop    / gWindowHeight;
	float ndcBottom = 1.0f - 2.0f * screenBottom / gWindowHeight;



	TQ3Point2D pts[4] =
			{
					//{ ndcLeft,	ndcBottom },		//		2----3
					//{ ndcRight,	ndcBottom },		//		| \  |
					//{ ndcLeft,	ndcTop },			//		|  \ |
					//{ ndcRight,	ndcTop },			//		0----1
					{ 0,	480 },	/*0*/	//		2----3
					{ 640,	480 },	/*1*/	//		| \  |
					{ 0,	0 },			/*2*/	//		|  \ |
					{ 640,	0 },			/*3*/	//		0----1
			};

	glColor4f(1, 1, 1, 1);
	EnableState(GL_TEXTURE_2D);
	EnableClientState(GL_TEXTURE_COORD_ARRAY);
	Render_BindTexture(texture);
	EnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, pts);
	glTexCoordPointer(2, GL_FLOAT, 0, kFullscreenQuadUVs);

	__glDrawRangeElements(GL_TRIANGLES, 0, 3*2, 3*2, GL_UNSIGNED_BYTE, kFullscreenQuadTriangles);
}

#if 0  // TODO: clean this up
static void Render_Draw2DFullscreenQuad(int fit)
{
	//		2----3
	//		| \  |
	//		|  \ |
	//		0----1
	TQ3Point2D pts[4] = {
			{-1,	-1},
			{ 1,	-1},
			{-1,	 1},
			{ 1,	 1},
	};

	float screenLeft   = 0.0f;
	float screenRight  = (float)gWindowWidth;
	float screenTop    = 0.0f;
	float screenBottom = (float)gWindowHeight;
	bool needClear = false;

	// Adjust screen coordinates if we want to pillarbox/letterbox the image.
	if (fit & (kCoverQuadLetterbox | kCoverQuadPillarbox))
	{
		const float targetAspectRatio = (float) gWindowWidth / gWindowHeight;
		const float sourceAspectRatio = (float) gCoverWindowTextureWidth / gCoverWindowTextureHeight;

		if (fabsf(sourceAspectRatio - targetAspectRatio) < 0.1)
		{
			// source and window have nearly the same aspect ratio -- fit (no-op)
		}
		else if ((fit & kCoverQuadLetterbox) && sourceAspectRatio > targetAspectRatio)
		{
			// source is wider than window -- letterbox
			needClear = true;
			float letterboxedHeight = gWindowWidth / sourceAspectRatio;
			screenTop = (gWindowHeight - letterboxedHeight) / 2;
			screenBottom = screenTop + letterboxedHeight;
		}
		else if ((fit & kCoverQuadPillarbox) && sourceAspectRatio < targetAspectRatio)
		{
			// source is narrower than window -- pillarbox
			needClear = true;
			float pillarboxedWidth = sourceAspectRatio * gWindowWidth / targetAspectRatio;
			screenLeft = (gWindowWidth / 2.0f) - (pillarboxedWidth / 2.0f);
			screenRight = screenLeft + pillarboxedWidth;
		}
	}

	// Compute normalized device coordinates for the quad vertices.
	float ndcLeft   = 2.0f * screenLeft  / gWindowWidth - 1.0f;
	float ndcRight  = 2.0f * screenRight / gWindowWidth - 1.0f;
	float ndcTop    = 1.0f - 2.0f * screenTop    / gWindowHeight;
	float ndcBottom = 1.0f - 2.0f * screenBottom / gWindowHeight;

	pts[0] = (TQ3Point2D) { ndcLeft, ndcBottom };
	pts[1] = (TQ3Point2D) { ndcRight, ndcBottom };
	pts[2] = (TQ3Point2D) { ndcLeft, ndcTop };
	pts[3] = (TQ3Point2D) { ndcRight, ndcTop };


	glColor4f(1, 1, 1, 1);
	EnableState(GL_TEXTURE_2D);
	EnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, pts);
	glTexCoordPointer(2, GL_FLOAT, 0, kFullscreenQuadUVs);
	__glDrawRangeElements(GL_TRIANGLES, 0, 3*2, 3*2, GL_UNSIGNED_BYTE, kFullscreenQuadTriangles);
}
#endif

#pragma mark -

//=======================================================================================================

/*******************************************/
/*    BACKDROP/OVERLAY (COVER WINDOW)      */
/*******************************************/

#if 0  // TODO: clean this up
void Render_Alloc2DCover(int width, int height)
{
	GAME_ASSERT_MESSAGE(gCoverWindowTextureName == 0, "cover texture already allocated");

	gCoverWindowTextureWidth = width;
	gCoverWindowTextureHeight = height;

	gCoverWindowTextureName = Render_LoadTexture(
			GL_RGBA,
			width,
			height,
			GL_BGRA,
			GL_UNSIGNED_INT_8_8_8_8,
			gCoverWindowPixPtr,
			kRendererTextureFlags_ClampBoth
	);

	ClearPortDamage();
}

void Render_Dispose2DCover(void)
{
	if (gCoverWindowTextureName == 0)
		return;

	glDeleteTextures(1, &gCoverWindowTextureName);
	gCoverWindowTextureName = 0;
}

void Render_Clear2DCover(UInt32 argb)
{
	UInt32 bgra = Byteswap32(&argb);

	UInt32* backdropPixPtr = gCoverWindowPixPtr;

	for (GLuint i = 0; i < gCoverWindowTextureWidth * gCoverWindowTextureHeight; i++)
	{
		*(backdropPixPtr++) = bgra;
	}

	GrafPtr port;
	GetPort(&port);
	DamagePortRegion(&port->portRect);
}

void Render_Draw2DCover(int fit)
{
	if (gCoverWindowTextureName == 0)
		return;

	Render_BindTexture(gCoverWindowTextureName);

	// If the screen port has dirty pixels ("damaged"), update the texture
	if (IsPortDamaged())
	{
		Rect damageRect;
		GetPortDamageRegion(&damageRect);

		// Set unpack row length to 640
		GLint pUnpackRowLength;
		glGetIntegerv(GL_UNPACK_ROW_LENGTH, &pUnpackRowLength);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, gCoverWindowTextureWidth);

		glTexSubImage2D(
				GL_TEXTURE_2D,
				0,
				damageRect.left,
				damageRect.top,
				damageRect.right - damageRect.left,
				damageRect.bottom - damageRect.top,
				GL_BGRA,
				GL_UNSIGNED_INT_8_8_8_8,
				gCoverWindowPixPtr + (damageRect.top * gCoverWindowTextureWidth + damageRect.left));
		CHECK_GL_ERROR();

		// Restore unpack row length
		glPixelStorei(GL_UNPACK_ROW_LENGTH, pUnpackRowLength);

		ClearPortDamage();
	}

	glViewport(0, 0, gWindowWidth, gWindowHeight);
	Render_Enter2D();
	Render_Draw2DFullscreenQuad(fit);
	Render_Exit2D();
}
#endif

static void DrawFadeOverlay(float opacity)
{
	glViewport(0, 0, gWindowWidth, gWindowHeight);
	Render_Enter2D_Full640x480();
	EnableState(GL_BLEND);
	DisableState(GL_TEXTURE_2D);
	DisableClientState(GL_TEXTURE_COORD_ARRAY);
	glColor4f(0, 0, 0, opacity);
	glVertexPointer(2, GL_FLOAT, 0, kFullscreenQuadPointsNDC);
	__glDrawRangeElements(GL_TRIANGLES, 0, 3*2, 3*2, GL_UNSIGNED_BYTE, kFullscreenQuadTriangles);
	Render_Exit2D_Full640x480();
}

#pragma mark -

void Render_SetWindowGamma(float percent)
{
	gFadeOverlayOpacity = (100.0f - percent) / 100.0f;
}

void Render_FreezeFrameFadeOut(void)
{
#if ALLOW_FADE
	//-------------------------------------------------------------------------
	// Capture window contents into texture

	int width4rem = gWindowWidth % 4;
	int width4ceil = gWindowWidth - width4rem + (width4rem == 0? 0: 4);

	GLint textureWidth = width4ceil;
	GLint textureHeight = gWindowHeight;
	char* textureData = NewPtrClear(textureWidth * textureHeight * 3);

	//SDL_GL_SwapWindow(gSDLWindow);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, textureWidth);
	glReadPixels(0, 0, textureWidth, textureHeight, GL_BGR, GL_UNSIGNED_BYTE, textureData);
	CHECK_GL_ERROR();

	GLuint textureName = Render_LoadTexture(
			GL_RGB,
			textureWidth,
			textureHeight,
			GL_BGR,
			GL_UNSIGNED_BYTE,
			textureData,
			kRendererTextureFlags_ClampBoth
			);
	CHECK_GL_ERROR();

	//-------------------------------------------------------------------------
	// Set up 2D viewport

	glViewport(0, 0, gWindowWidth, gWindowHeight);
	Render_Enter2D();
	DisableState(GL_BLEND);
	EnableState(GL_TEXTURE_2D);
	EnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, kFullscreenQuadPointsNDC);
	glTexCoordPointer(2, GL_FLOAT, 0, kFullscreenQuadUVsFlipped);

	//-------------------------------------------------------------------------
	// Fade out

	Uint32 startTicks = SDL_GetTicks();
	Uint32 endTicks = startTicks + kFreezeFrameFadeOutDuration * 1000.0f;

	for (Uint32 ticks = startTicks; ticks <= endTicks; ticks = SDL_GetTicks())
	{
		float gGammaFadePercent = 1.0f - ((ticks - startTicks) / 1000.0f / kFreezeFrameFadeOutDuration);
		if (gGammaFadePercent < 0.0f)
			gGammaFadePercent = 0.0f;

		glColor4f(gGammaFadePercent, gGammaFadePercent, gGammaFadePercent, 1.0f);
		__glDrawRangeElements(GL_TRIANGLES, 0, 3*2, 3*2, GL_UNSIGNED_BYTE, kFullscreenQuadTriangles);
		CHECK_GL_ERROR();
		SDL_GL_SwapWindow(gSDLWindow);
		SDL_Delay(15);
	}

	//-------------------------------------------------------------------------
	// Hold full blackness for a little bit

	startTicks = SDL_GetTicks();
	endTicks = startTicks + .1f * 1000.0f;
	glClearColor(0,0,0,1);
	for (Uint32 ticks = startTicks; ticks <= endTicks; ticks = SDL_GetTicks())
	{
		glClear(GL_COLOR_BUFFER_BIT);
		SDL_GL_SwapWindow(gSDLWindow);
		SDL_Delay(15);
	}

	//-------------------------------------------------------------------------
	// Clean up

	Render_Exit2D();

	DisposePtr(textureData);
	glDeleteTextures(1, &textureName);

	gFadeOverlayOpacity = 1;
#endif
}

#pragma mark -

TQ3Area Render_GetAdjustedViewportRect(Rect paneClip, int logicalWidth, int logicalHeight)
{
	float scaleX = gWindowWidth / (float)logicalWidth;	// scale clip pane to window size
	float scaleY = gWindowHeight / (float)logicalHeight;

	// Floor min to avoid seam at edges of HUD if scale ratio is dirty
	float left = floorf( scaleX * paneClip.left );
	float top = floorf( scaleY * paneClip.top  );
	// Ceil max to avoid seam at edges of HUD if scale ratio is dirty
	float right = ceilf( scaleX * (logicalWidth  - paneClip.right ) );
	float bottom = ceilf( scaleY * (logicalHeight - paneClip.bottom) );

	return (TQ3Area) {{left,top},{right,bottom}};
}
