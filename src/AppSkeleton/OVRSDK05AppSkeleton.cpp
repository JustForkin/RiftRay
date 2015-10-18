// OVRSDK05AppSkeleton.cpp

#ifdef _WIN32
#  define WINDOWS_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif
#include <GL/glew.h>

#include <OVR.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define _USE_MATH_DEFINES
#include <math.h>
#include <algorithm>
#include <iostream>
#include <sstream>

#include "OVRSDK05AppSkeleton.h"
#include "ShaderToy.h"
#include "ShaderFunctions.h"
#include "MatrixFunctions.h"
#include "GLUtils.h"
#include "Logger.h"

OVRSDK05AppSkeleton::OVRSDK05AppSkeleton()
: AppSkeleton()
, m_Hmd(NULL)
, m_usingDebugHmd(false)
, m_directHmdMode(true)
{
    m_eyePoseCached = OVR::Posef();
    _StoreHmdPose(m_eyePoseCached);

    ResetChassisTransformations();
}

OVRSDK05AppSkeleton::~OVRSDK05AppSkeleton()
{
}

void OVRSDK05AppSkeleton::RecenterPose()
{
    if (m_Hmd == NULL)
        return;
    ovrHmd_RecenterPose(m_Hmd);
}

ovrSizei OVRSDK05AppSkeleton::getHmdResolution() const
{
    if (m_Hmd == NULL)
    {
        ovrSizei empty = {0, 0};
        return empty;
    }
    return m_Hmd->Resolution;
}

ovrVector2i OVRSDK05AppSkeleton::getHmdWindowPos() const
{
    if (m_Hmd == NULL)
    {
        ovrVector2i empty = {0, 0};
        return empty;
    }
    return m_Hmd->WindowsPos;
}

void OVRSDK05AppSkeleton::initGL()
{
    AppSkeleton::initGL();

    // sensible initial value?
    allocateFBO(m_rwwttBuffer, 800, 600);
}

///@brief Set this up early so we can get the HMD's display dimensions to create a window.
void OVRSDK05AppSkeleton::initHMD()
{
    ovr_Initialize();

    m_Hmd = ovrHmd_Create(0);
    if (m_Hmd == NULL)
    {
        m_Hmd = ovrHmd_CreateDebug(ovrHmd_DK1);
        m_usingDebugHmd = true;
        m_directHmdMode = false;
    }

    const unsigned int caps = m_Hmd->HmdCaps;
    if ((caps & ovrHmdCap_ExtendDesktop) != 0)
    {
        m_directHmdMode = false;
    }

    if (m_Hmd != NULL)
    {
        const ovrBool ret = ovrHmd_ConfigureTracking(m_Hmd,
            ovrTrackingCap_Orientation | ovrTrackingCap_MagYawCorrection | ovrTrackingCap_Position,
            ovrTrackingCap_Orientation);
        if (ret == 0)
        {
            std::cerr << "Error calling ovrHmd_ConfigureTracking." << std::endl;
        }
    }

    m_ovrScene.SetHmdPointer(m_Hmd);
}

void OVRSDK05AppSkeleton::initVR()
{
    m_Cfg.OGL.Header.BackBufferSize = getHmdResolution();

    ConfigureRendering();
    ConfigureSDKRendering();
    ConfigureClientRendering();

    _initPresentDistMesh(m_presentDistMeshL, 0);
    _initPresentDistMesh(m_presentDistMeshR, 1);

    if (UsingDebugHmd() == false)
    {
        m_windowSize.x = m_EyeTexture[0].OGL.Header.RenderViewport.Size.w;
        m_windowSize.y = m_EyeTexture[0].OGL.Header.RenderViewport.Size.h;
    }
}

void OVRSDK05AppSkeleton::_initPresentDistMesh(ShaderWithVariables& shader, int eyeIdx)
{
    // Init left and right VAOs separately
    shader.bindVAO();

    ovrDistortionMesh& mesh = m_DistMeshes[eyeIdx];
    GLuint vertVbo = 0;
    glGenBuffers(1, &vertVbo);
    shader.AddVbo("vPosition", vertVbo);
    glBindBuffer(GL_ARRAY_BUFFER, vertVbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.VertexCount * sizeof(ovrDistortionVertex), &mesh.pVertexData[0].ScreenPosNDC.x, GL_STATIC_DRAW);

    const int a_pos = shader.GetAttrLoc("vPosition");
    glVertexAttribPointer(a_pos, 4, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex), NULL);
    glEnableVertexAttribArray(a_pos);

    const int a_texR = shader.GetAttrLoc("vTexR");
    if (a_texR > -1)
    {
        glVertexAttribPointer(a_texR, 2, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex),
            reinterpret_cast<void*>(offsetof(ovrDistortionVertex, TanEyeAnglesR)));
        glEnableVertexAttribArray(a_texR);
    }

    const int a_texG = shader.GetAttrLoc("vTexG");
    if (a_texG > -1)
    {
        glVertexAttribPointer(a_texG, 2, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex),
            reinterpret_cast<void*>(offsetof(ovrDistortionVertex, TanEyeAnglesG)));
        glEnableVertexAttribArray(a_texG);
    }

    const int a_texB = shader.GetAttrLoc("vTexB");
    if (a_texB > -1)
    {
        glVertexAttribPointer(a_texB, 2, GL_FLOAT, GL_FALSE, sizeof(ovrDistortionVertex),
            reinterpret_cast<void*>(offsetof(ovrDistortionVertex, TanEyeAnglesB)));
        glEnableVertexAttribArray(a_texB);
    }

    GLuint elementVbo = 0;
    glGenBuffers(1, &elementVbo);
    shader.AddVbo("elements", elementVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementVbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.IndexCount * sizeof(GLushort), &mesh.pIndexData[0], GL_STATIC_DRAW);

    // We have copies of the mesh in GL, but keep a count of indices around for the GL draw call.
    const unsigned int tmp = mesh.IndexCount;
    ovrHmd_DestroyDistortionMesh(&mesh);
    mesh.IndexCount = tmp;

    glBindVertexArray(0);
}

void OVRSDK05AppSkeleton::exitVR()
{
    AppSkeleton::exitGL();
    deallocateFBO(m_rwwttBuffer);
    ovrHmd_Destroy(m_Hmd);
    ovr_Shutdown();
}

/// Add together the render target size fields of the HMD laid out side-by-side.
ovrSizei calculateCombinedTextureSize(ovrHmd pHmd)
{
    ovrSizei texSz = {0};
    if (pHmd == NULL)
        return texSz;

    const ovrSizei szL = ovrHmd_GetFovTextureSize(pHmd, ovrEye_Left, pHmd->DefaultEyeFov[ovrEye_Left], 1.f);
    const ovrSizei szR = ovrHmd_GetFovTextureSize(pHmd, ovrEye_Right, pHmd->DefaultEyeFov[ovrEye_Right], 1.f);
    texSz.w = szL.w + szR.w;
    texSz.h = std::max(szL.h, szR.h);
    return texSz;
}

///@brief Writes to m_EyeTexture and m_EyeFov
int OVRSDK05AppSkeleton::ConfigureRendering()
{
    if (m_Hmd == NULL)
        return 1;

    const ovrSizei texSz = calculateCombinedTextureSize(m_Hmd);
    deallocateFBO(m_renderBuffer);
    deallocateFBO(m_rwwttBuffer);
    allocateFBO(m_renderBuffer, texSz.w, texSz.h);
    allocateFBO(m_rwwttBuffer, texSz.w, texSz.h);

    ovrGLTexture& texL = m_EyeTexture[ovrEye_Left];
    ovrGLTextureData& texDataL = texL.OGL;
    ovrTextureHeader& hdrL = texDataL.Header;

    hdrL.API = ovrRenderAPI_OpenGL;
    hdrL.TextureSize.w = texSz.w;
    hdrL.TextureSize.h = texSz.h;
    hdrL.RenderViewport.Pos.x = 0;
    hdrL.RenderViewport.Pos.y = 0;
    hdrL.RenderViewport.Size.w = texSz.w / 2;
    hdrL.RenderViewport.Size.h = texSz.h;
    texDataL.TexId = m_renderBuffer.tex;

    // Right eye the same, except for the x-position in the texture.
    ovrGLTexture& texR = m_EyeTexture[ovrEye_Right];
    texR = texL;
    texR.OGL.Header.RenderViewport.Pos.x = (texSz.w + 1) / 2;

    for (int ei=0; ei<ovrEye_Count; ++ei)
    {
        m_EyeFov[ei] = m_Hmd->DefaultEyeFov[ei];
    }

    return 0;
}

///@brief Active GL context is required for the following
/// Writes to m_Cfg
int OVRSDK05AppSkeleton::ConfigureSDKRendering()
{
    if (m_Hmd == NULL)
        return 1;

    m_Cfg.OGL.Header.API = ovrRenderAPI_OpenGL;
    m_Cfg.OGL.Header.Multisample = 0;

    const int distortionCaps =
        ovrDistortionCap_TimeWarp |
        ovrDistortionCap_Vignette;
    ovrHmd_ConfigureRendering(m_Hmd, &m_Cfg.Config, distortionCaps, m_EyeFov, m_EyeRenderDesc);

    return 0;
}

///@brief Writes to m_EyeRenderDesc, m_EyeRenderDesc and m_DistMeshes
int OVRSDK05AppSkeleton::ConfigureClientRendering()
{
    if (m_Hmd == NULL)
        return 1;

    const int distortionCaps =
        ovrDistortionCap_TimeWarp |
        ovrDistortionCap_Vignette;
    for (int eyeIndex = 0; eyeIndex < ovrEye_Count; eyeIndex++)
    {
        // Using an idiomatic loop though initializing in eye render order is not necessary.
        const ovrEyeType eye = m_Hmd->EyeRenderOrder[eyeIndex];

        m_EyeRenderDesc[eye] = ovrHmd_GetRenderDesc(m_Hmd, eye, m_EyeFov[eye]);
        m_RenderViewports[eye] = m_EyeTexture[eye].OGL.Header.RenderViewport;
        ovrHmd_CreateDistortionMesh(
            m_Hmd,
            m_EyeRenderDesc[eye].Eye,
            m_EyeRenderDesc[eye].Fov,
            distortionCaps,
            &m_DistMeshes[eye]);
    }
    return 0;
}

///@brief The HSW will be displayed by default when using SDK rendering.
void OVRSDK05AppSkeleton::DismissHealthAndSafetyWarning() const
{
    ovrHSWDisplayState hswDisplayState;
    ovrHmd_GetHSWDisplayState(m_Hmd, &hswDisplayState);
    if (hswDisplayState.Displayed)
    {
        ovrHmd_DismissHSWDisplay(m_Hmd);
    }
}

///@brief This function will detect a moderate tap on the Rift via the accelerometer.
///@return true if a tap was detected, false otherwise.
bool OVRSDK05AppSkeleton::CheckForTapOnHmd()
{
    const ovrTrackingState ts = ovrHmd_GetTrackingState(m_Hmd, ovr_GetTimeInSeconds());
    if (!(ts.StatusFlags & ovrStatus_OrientationTracked))
        return false;

    const OVR::Vector3f v(ts.RawSensorData.Accelerometer);
    // Arbitrary value and representing moderate tap on the side of the DK2 Rift.
    // When HMD is stationary, gravity alone should yield ~= 9.8^2 == 96.04
    const float lenSq = v.LengthSq();
    const float tapThreshold = 250.f;
    if (lenSq > tapThreshold)
    {
        // Limit tapping rate
        static double lastTapTime = 0.0;
        if (ovr_GetTimeInSeconds() - lastTapTime > 0.5)
        {
            lastTapTime = ovr_GetTimeInSeconds();
            DismissHealthAndSafetyWarning();
            ToggleShaderWorld();
            return true;
        }
    }
    return false;
}

/// Uses a cached copy of HMD orientation written to in display(which are const
/// functions, but m_eyePoseCached is a mutable member).
glm::mat4 OVRSDK05AppSkeleton::makeWorldToEyeMatrix() const
{
    return makeWorldToChassisMatrix() * makeMatrixFromPose(m_eyePoseCached, m_headSize);
}

// Store HMD position and direction for gaze tracking in timestep.
// OVR SDK requires head pose be queried between ovrHmd_BeginFrameTiming and ovrHmd_EndFrameTiming.
// Don't worry - we're just writing to _mutable_ members, it's still const!
void OVRSDK05AppSkeleton::_StoreHmdPose(const ovrPosef& eyePose) const
{
    m_hmdRo.x = eyePose.Position.x + m_chassisPos.x;
    m_hmdRo.y = eyePose.Position.y + m_chassisPos.y;
    m_hmdRo.z = eyePose.Position.z + m_chassisPos.z;

    const glm::mat4 w2eye = makeWorldToChassisMatrix() * makeMatrixFromPose(eyePose, m_headSize);
    const OVR::Matrix4f rotmtx = makeOVRMatrixFromGlmMatrix(w2eye);
    const OVR::Vector4f lookFwd(0.f, 0.f, -1.f, 0.f);
    const OVR::Vector4f rotvec = rotmtx.Transform(lookFwd);
    m_hmdRd.x = rotvec.x;
    m_hmdRd.y = rotvec.y;
    m_hmdRd.z = rotvec.z;

    // Store a separate copy of (ro,rd) in local space without chassis txfms applied.
    m_hmdRoLocal.x = eyePose.Position.x;
    m_hmdRoLocal.y = eyePose.Position.y;
    m_hmdRoLocal.z = eyePose.Position.z;

    const OVR::Matrix4f rotmtxLocal = OVR::Matrix4f(eyePose.Orientation);
    const OVR::Vector4f rotvecLocal = rotmtxLocal.Transform(OVR::Vector4f(0.0f, 0.0f, -1.0f, 0.0f));
    m_hmdRdLocal.x = rotvecLocal.x;
    m_hmdRdLocal.y = rotvecLocal.y;
    m_hmdRdLocal.z = rotvecLocal.z;

    m_eyePoseCached = eyePose; // cache this for movement direction
}

///@todo Even though this function shares most of its code with client rendering,
/// which appears to work fine, it is non-convergable. It appears that the projection
/// matrices for each eye are too far apart? Could be modelview...
void OVRSDK05AppSkeleton::display_stereo_undistorted() const
{
    ovrHmd hmd = m_Hmd;
    if (hmd == NULL)
        return;

    //ovrFrameTiming hmdFrameTiming =
    //ovrHmd_BeginFrame(m_Hmd, 0);

    bindFBO(m_renderBuffer, m_fboScale);

    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ovrVector3f e2v[2] = {
        OVR::Vector3f(m_EyeRenderDesc[0].HmdToEyeViewOffset),
        OVR::Vector3f(m_EyeRenderDesc[1].HmdToEyeViewOffset),
    };
    ovrVector3f e2vScaled[2] = {
        OVR::Vector3f(e2v[0]) * m_headSize,
        OVR::Vector3f(e2v[1]) * m_headSize,
    };

    ovrTrackingState outHmdTrackingState;
    ovrPosef outEyePoses[2];
    ovrHmd_GetEyePoses(
        hmd,
        0,
        e2v, // could this parameter be const?
        outEyePoses,
        &outHmdTrackingState);

    ovrPosef outEyePosesScaled[2];
    ovrHmd_GetEyePoses(
        hmd,
        0, ///@todo Frame index
        e2vScaled, // could this parameter be const?
        outEyePosesScaled,
        &outHmdTrackingState);

    // For passing to EndFrame once rendering is done
    ovrPosef renderPose[2];
    ovrTexture eyeTexture[2];
    for (int eyeIndex = 0; eyeIndex < ovrEye_Count; eyeIndex++)
    {
        const ovrEyeType e = hmd->EyeRenderOrder[eyeIndex];

        const ovrPosef eyePose = outEyePoses[e];
        renderPose[e] = eyePose;
        eyeTexture[e] = m_EyeTexture[e].Texture;
        m_eyePoseCached = eyePose; // cache this for movement direction
        _StoreHmdPose(eyePose);

        const ovrGLTexture& otex = m_EyeTexture[e];
        const ovrRecti& rvp = otex.OGL.Header.RenderViewport;
        const rect rsc = {
            glm::ivec2(
                static_cast<int>(m_fboScale * rvp.Pos.x),
                static_cast<int>(m_fboScale * rvp.Pos.y)),
            glm::ivec2(
                static_cast<int>(m_fboScale * rvp.Size.w),
                static_cast<int>(m_fboScale * rvp.Size.h))
        };
        glViewport(rsc.Pos.x, rsc.Pos.y, rsc.Size.x, rsc.Size.y);

        const OVR::Matrix4f proj = ovrMatrix4f_Projection(
            m_EyeRenderDesc[e].Fov,
            0.01f, 10000.0f, true);

        const ovrPosef eyePoseScaled = outEyePosesScaled[e];
        const glm::mat4 viewLocal = makeMatrixFromPose(eyePose);
        const glm::mat4 viewLocalScaled = makeMatrixFromPose(eyePoseScaled, m_headSize);
        const glm::mat4 viewWorld = makeWorldToChassisMatrix() * viewLocalScaled;

        _resetGLState();

        _DrawScenes(
            glm::value_ptr(glm::inverse(viewWorld)),
            &proj.Transposed().M[0][0],
            rsc,
            glm::value_ptr(glm::inverse(viewLocal))
            );
    }
    unbindFBO();

    //ovrHmd_EndFrame(m_Hmd, renderPose, eyeTexture);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    const int w = m_Cfg.OGL.Header.BackBufferSize.w;
    const int h = m_Cfg.OGL.Header.BackBufferSize.h;
    glViewport(0, 0, w, h);

    // Present FBO to screen
    const GLuint prog = m_presentFbo.prog();
    glUseProgram(prog);
    m_presentFbo.bindVAO();
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_renderBuffer.tex);
        glUniform1i(m_presentFbo.GetUniLoc("fboTex"), 0);
        glUniform1f(m_presentFbo.GetUniLoc("fboScale"), m_fboScale);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
    glBindVertexArray(0);
    glUseProgram(0);
}

const ovrRecti getScaledRect(const ovrRecti& r, float s)
{
    const ovrRecti scaled = {
        static_cast<int>(s * r.Pos.x),
        static_cast<int>(s * r.Pos.y),
        static_cast<int>(s * r.Size.w),
        static_cast<int>(s * r.Size.h)
    };
    return scaled;
}

///@brief Draw all scenes to a 
void OVRSDK05AppSkeleton::_RenderScenesToStereoBuffer(
    const ovrHmd hmd,
    const glm::mat4* eyeProjMatrix,
    const glm::mat4* eyeMvMtxLocal,
    const glm::mat4* eyeMvMtxWorld,
    const ovrRecti* rvpFull
    ) const
{
    if (hmd == NULL)
        return;

    float fboScale = m_fboScale;

    // Draw to the surface that will be presented to OVR SDK via ovrHmd_EndFrame
    bindFBO(m_rwwttBuffer);
    {
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_SCISSOR_TEST); // cinemaScope letterbox bars
        for (std::vector<IScene*>::const_iterator it = m_scenes.begin();
            it != m_scenes.end();
            ++it)
        {
            const IScene* pScene = *it;
            if (pScene == NULL)
                continue;

            // Pre-render per-scene actions
            // These blocks depend on the order scenes were added to the vector.
            const bool doRaymarch = m_galleryScene.GetActiveShaderToy() != NULL;
            if (pScene == &m_galleryScene)
            {
                if (doRaymarch)
                {
                    glDisable(GL_DEPTH_TEST);
                    glDepthMask(GL_FALSE);
                }
                else
                {
                    // A state object would be nice here, disable scissoring temporarily.
                    bindFBO(m_renderBuffer);
                    glDisable(GL_SCISSOR_TEST); // disable cinemascope scissor to fill render target
                    glClearColor(0.f, 0.f, 0.f, 0.f);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    glEnable(GL_SCISSOR_TEST); // re-enable for cinemascope
                    fboScale = 1.f;
                }
            }
            else if (pScene == &m_ovrScene)
            {
                if (doRaymarch)
                    glDepthMask(GL_TRUE);
            }
            else if (pScene == &m_floorScene)
            {
                if (doRaymarch)
                {
                    glEnable(GL_DEPTH_TEST);
                    break; // out of Scene loop; no need to draw floor
                }
            }

            // Draw eyes inside scene loop
            for (int eyeIndex=0; eyeIndex<ovrEye_Count; eyeIndex++)
            {
                const ovrEyeType e = hmd->EyeRenderOrder[eyeIndex];

                // Viewport setup
                const ovrRecti rvpScaled = getScaledRect(rvpFull[e], fboScale);
                const ovrRecti& rvp = rvpScaled;
                const int yoff = static_cast<int>(static_cast<float>(rvp.Size.h) * m_cinemaScopeFactor);
                glViewport(rvp.Pos.x, rvp.Pos.y, rvp.Size.w, rvp.Size.h);
                glScissor(0, yoff/2, rvp.Pos.x+rvp.Size.w, rvp.Size.h-yoff); // Assume side-by-side single render texture

                // Matrix setup
                const float* pPersp = glm::value_ptr(eyeProjMatrix[e]);
                const float* pMvWorld = glm::value_ptr(eyeMvMtxWorld[e]);
                const float* pMvLocal = glm::value_ptr(eyeMvMtxLocal[e]);
                const float* pMv = pScene->m_bChassisLocalSpace ? pMvLocal : pMvWorld;

                pScene->RenderForOneEye(pMv, pPersp);
            } // eye loop

            // Post-render scene-specific actions
            if (doRaymarch && (pScene == &m_galleryScene))
            {
                // rwwtt scene is now rendered to downscaled buffer.
                // Stay bound to this FBO for UI accoutrements rendering.
                glDisable(GL_SCISSOR_TEST); // disable cinemascope scissor to fill render target
                bindFBO(m_renderBuffer);
                _StretchBlitDownscaledBuffer();
                glEnable(GL_SCISSOR_TEST); // re-enable for cinemascope
                fboScale = 1.f;
            }

        } // scene loop
        glDisable(GL_SCISSOR_TEST); // cinemaScope letterbox bars
    }
    unbindFBO();
}

///@brief The extra blit imposes some overhead, so for better performance we can render
/// only the raymarch scene to a downscaled buffer and pass that directly to OVR.
void OVRSDK05AppSkeleton::_RenderOnlyRaymarchSceneToStereoBuffer(
    const ovrHmd hmd,
    const glm::mat4* eyeProjMatrix,
    const glm::mat4* eyeMvMtxWorld,
    const ovrRecti* rvpFull) const
{
    const float fboScale = m_fboScale;
    bindFBO(m_renderBuffer);
    {
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);

        // Draw eyes inside scene loop
        for (int eyeIndex=0; eyeIndex<ovrEye_Count; eyeIndex++)
        {
            const ovrEyeType e = hmd->EyeRenderOrder[eyeIndex];

            // Viewport setup
            const ovrRecti rvpScaled = getScaledRect(rvpFull[e], fboScale);
            const ovrRecti& rvp = rvpScaled;
            const int yoff = static_cast<int>(static_cast<float>(rvp.Size.h) * m_cinemaScopeFactor);
            glViewport(rvp.Pos.x, rvp.Pos.y, rvp.Size.w, rvp.Size.h);
            glScissor(0, yoff/2, rvp.Pos.x+rvp.Size.w, rvp.Size.h-yoff); // Assume side-by-side single render texture

            // Matrix setup
            const float* pPersp = glm::value_ptr(eyeProjMatrix[e]);
            const float* pMvWorld = glm::value_ptr(eyeMvMtxWorld[e]);

            m_galleryScene.RenderForOneEye(pMvWorld, pPersp);
        } // eye loop

        glDisable(GL_SCISSOR_TEST);
    }
    unbindFBO();
}

///@brief Blit the contents of the downscaled render buffer to the full-sized
void OVRSDK05AppSkeleton::_StretchBlitDownscaledBuffer() const
{
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const GLuint prog = m_presentFbo.prog();
    glUseProgram(prog);
    m_presentFbo.bindVAO();
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_rwwttBuffer.tex);
        glUniform1i(m_presentFbo.GetUniLoc("fboTex"), 0);
        glUniform1f(m_presentFbo.GetUniLoc("fboScale"), m_fboScale);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
    glBindVertexArray(0);
    glUseProgram(0);
}

///@brief Populate the given arrrays of size eyeCount with per-eye rendering parameters.
void OVRSDK05AppSkeleton::_CalculatePerEyeRenderParams(
    const ovrPosef eyePoses[2], // [in] Eye poses in local space from ovrHmd_GetEyePoses
    const ovrPosef eyePosesScaled[2], // [in] Eye poses from ovrHmd_GetEyePoses with head size applied
    ovrPosef* renderPose, // [out]
    ovrTexture* eyeTexture, // [out]
    glm::mat4* eyeProjMatrix, // [out]
    glm::mat4* eyeMvMtxLocal, // [out]
    glm::mat4* eyeMvMtxWorld, // [out]
    ovrRecti* renderVp // [out]
    ) const
{
    ovrHmd hmd = m_Hmd;
    if (hmd == NULL)
        return;

    // Calculate eye poses for rendering and to pass to OVR SDK after rendering
    for (int eyeIndex=0; eyeIndex<ovrEye_Count; eyeIndex++)
    {
        const ovrEyeType e = hmd->EyeRenderOrder[eyeIndex];
        const ovrPosef eyePose = eyePoses[e];
        const ovrPosef eyePoseScaled = eyePosesScaled[e];
        const ovrGLTexture& otex = m_EyeTexture[e];
        const ovrEyeRenderDesc& erd = m_EyeRenderDesc[e];

        renderPose[e] = eyePose;
        eyeTexture[e] = otex.Texture;
        renderVp[e] = otex.OGL.Header.RenderViewport;

        const OVR::Matrix4f proj = ovrMatrix4f_Projection(erd.Fov, 0.01f, 10000.0f, true);
        eyeProjMatrix[e] = glm::make_mat4(&proj.Transposed().M[0][0]);
        eyeMvMtxLocal[e] = makeMatrixFromPose(eyePose);
        const glm::mat4 eyeMvMtxLocalScaled = makeMatrixFromPose(eyePoseScaled, m_headSize);
        eyeMvMtxWorld[e] = makeWorldToChassisMatrix() * eyeMvMtxLocalScaled;
        // These two matrices will be used directly for rendering
        eyeMvMtxLocal[e] = glm::inverse(eyeMvMtxLocal[e]);
        eyeMvMtxWorld[e] = glm::inverse(eyeMvMtxWorld[e]);

        if (eyeIndex == 0)
        {
            _StoreHmdPose(eyePose);
        }
    }
}

void OVRSDK05AppSkeleton::display_sdk() const
{
    ovrHmd hmd = m_Hmd;
    if (hmd == NULL)
        return;

    //const ovrFrameTiming hmdFrameTiming =
    ovrHmd_BeginFrame(hmd, 0);

    ovrVector3f e2v[2] = {
        OVR::Vector3f(m_EyeRenderDesc[0].HmdToEyeViewOffset),
        OVR::Vector3f(m_EyeRenderDesc[1].HmdToEyeViewOffset),
    };
    ovrVector3f e2vScaled[2] = {
        OVR::Vector3f(e2v[0]) * m_headSize,
        OVR::Vector3f(e2v[1]) * m_headSize,
    };

    ovrTrackingState ohts;
    ovrPosef outEyePoses[2];
    ovrPosef outEyePosesScaled[2];
    ovrHmd_GetEyePoses(hmd, 0, e2v, outEyePoses, &ohts);
    ovrHmd_GetEyePoses(hmd, 0, e2vScaled, outEyePosesScaled, &ohts);

    ovrPosef renderPose[ovrEye_Count]; // Pass to ovrHmd_EndFrame post-rendering
    ovrTexture eyeTexture[ovrEye_Count]; // Pass to ovrHmd_EndFrame post-rendering
    glm::mat4 eyeProjMatrix[ovrEye_Count];
    glm::mat4 eyeMvMtxLocal[ovrEye_Count];
    glm::mat4 eyeMvMtxWorld[ovrEye_Count];
    ovrRecti renderVp[ovrEye_Count];
    _CalculatePerEyeRenderParams(outEyePoses, outEyePosesScaled, renderPose, eyeTexture, eyeProjMatrix, eyeMvMtxLocal, eyeMvMtxWorld, renderVp);

    _resetGLState();

    const bool doRaymarch = m_galleryScene.GetActiveShaderToy() != NULL;
    const bool drawBar = m_dashScene.m_bDraw;
    if (doRaymarch && !drawBar)
    {
        _RenderOnlyRaymarchSceneToStereoBuffer(hmd, eyeProjMatrix, eyeMvMtxWorld, renderVp);

        // Inform SDK of downscaled texture target size(performance scaling)
        for (int i=0; i<ovrEye_Count; ++i)
        {
            const ovrSizei& ts = m_EyeTexture[i].Texture.Header.TextureSize;
            ovrRecti& rr = eyeTexture[i].Header.RenderViewport;
            rr.Size.w = static_cast<int>(static_cast<float>(ts.w/2) * m_fboScale);
            rr.Size.h = static_cast<int>(static_cast<float>(ts.h) * m_fboScale);
            rr.Pos.x = i * rr.Size.w;
        }
    }
    else
    {
        _RenderScenesToStereoBuffer(hmd, eyeProjMatrix, eyeMvMtxLocal, eyeMvMtxWorld, renderVp);
    }

    ovrHmd_EndFrame(hmd, renderPose, eyeTexture);
}

void OVRSDK05AppSkeleton::display_client() const
{
    ovrHmd hmd = m_Hmd;
    if (hmd == NULL)
        return;

    //const ovrFrameTiming hmdFrameTiming =
    ovrHmd_BeginFrame(hmd, 0);

    ovrVector3f e2v[2] = {
        OVR::Vector3f(m_EyeRenderDesc[0].HmdToEyeViewOffset),
        OVR::Vector3f(m_EyeRenderDesc[1].HmdToEyeViewOffset),
    };
    ovrVector3f e2vScaled[2] = {
        OVR::Vector3f(e2v[0]) * m_headSize,
        OVR::Vector3f(e2v[1]) * m_headSize,
    };

    ovrTrackingState ohts;
    ovrPosef outEyePoses[2];
    ovrPosef outEyePosesScaled[2];
    ovrHmd_GetEyePoses(hmd, 0, e2v, outEyePoses, &ohts);
    ovrHmd_GetEyePoses(hmd, 0, e2vScaled, outEyePosesScaled, &ohts);

    ovrPosef renderPose[ovrEye_Count]; // Pass to ovrHmd_EndFrame post-rendering
    ovrTexture eyeTexture[ovrEye_Count]; // Pass to ovrHmd_EndFrame post-rendering
    glm::mat4 eyeProjMatrix[ovrEye_Count];
    glm::mat4 eyeMvMtxLocal[ovrEye_Count];
    glm::mat4 eyeMvMtxWorld[ovrEye_Count];
    ovrRecti renderVp[ovrEye_Count];
    _CalculatePerEyeRenderParams(outEyePoses, outEyePosesScaled, renderPose, eyeTexture, eyeProjMatrix, eyeMvMtxLocal, eyeMvMtxWorld, renderVp);

    _resetGLState();

    const bool doRaymarch = m_galleryScene.GetActiveShaderToy() != NULL;
    const bool drawBar = m_dashScene.m_bDraw;
    if (doRaymarch && !drawBar)
    {
        _RenderOnlyRaymarchSceneToStereoBuffer(hmd, eyeProjMatrix, eyeMvMtxWorld, renderVp);

        // Inform SDK of downscaled texture target size(performance scaling)
        for (int i=0; i<ovrEye_Count; ++i)
        {
            const ovrSizei& ts = m_EyeTexture[i].Texture.Header.TextureSize;
            ovrRecti& rr = eyeTexture[i].Header.RenderViewport;
            rr.Size.w = static_cast<int>(static_cast<float>(ts.w/2) * m_fboScale);
            rr.Size.h = static_cast<int>(static_cast<float>(ts.h) * m_fboScale);
            rr.Pos.x = i * rr.Size.w;
        }
    }
    else
    {
        _RenderScenesToStereoBuffer(hmd, eyeProjMatrix, eyeMvMtxLocal, eyeMvMtxWorld, renderVp);
    }

    // Set full viewport for presentation to Rift display
    const int w = m_Cfg.OGL.Header.BackBufferSize.w;
    const int h = m_Cfg.OGL.Header.BackBufferSize.h;
    glViewport(0, 0, w, h);

    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Now draw the distortion mesh...
    for(int eyeNum = 0; eyeNum < 2; eyeNum++)
    {
        const ShaderWithVariables& eyeShader = eyeNum == 0 ?
            m_presentDistMeshL :
            m_presentDistMeshR;
        const GLuint prog = eyeShader.prog();
        glUseProgram(prog);
        eyeShader.bindVAO();
        {
            const ovrDistortionMesh& mesh = m_DistMeshes[eyeNum];

            ovrVector2f uvScaleOffsetOut[2];
            ovrHmd_GetRenderScaleAndOffset(
                m_EyeFov[eyeNum],
                m_EyeTexture[eyeNum].Texture.Header.TextureSize,
                m_EyeTexture[eyeNum].OGL.Header.RenderViewport,
                uvScaleOffsetOut );

            const ovrVector2f uvscale = uvScaleOffsetOut[0];
            const ovrVector2f uvoff = uvScaleOffsetOut[1];

            glUniform2f(eyeShader.GetUniLoc("EyeToSourceUVOffset"), uvoff.x, uvoff.y);
            glUniform2f(eyeShader.GetUniLoc("EyeToSourceUVScale"), uvscale.x, uvscale.y);

#if 0
            if (distortionCaps & ovrDistortionCap_TimeWarp)
            { // TIMEWARP - Additional shader constants required
                ovrMatrix4f timeWarpMatrices[2];
                ovrHmd_GetEyeTimewarpMatrices(HMD, (ovrEyeType)eyeNum, eyeRenderPoses[eyeNum], timeWarpMatrices);
                //WARNING!!! These matrices are transposed in SetUniform4x4f, before being used by the shader.
                DistortionData.Shaders->SetUniform4x4f("EyeRotationStart", Matrix4f(timeWarpMatrices[0]));
                DistortionData.Shaders->SetUniform4x4f("EyeRotationEnd", Matrix4f(timeWarpMatrices[1]));
            }

            // Perform distortion
            pRender->Render(
                &distortionShaderFill,
                DistortionData.MeshVBs[eyeNum],
                DistortionData.MeshIBs[eyeNum]);
#endif

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_renderBuffer.tex);
            glUniform1i(eyeShader.GetUniLoc("fboTex"), 0);

            float fboScale = 1.f;
            if (doRaymarch && !drawBar)
                fboScale = m_fboScale;
            glUniform1f(eyeShader.GetUniLoc("fboScale"), fboScale);

            glDrawElements(
                GL_TRIANGLES,
                mesh.IndexCount,
                GL_UNSIGNED_SHORT,
                0);
        }
        glBindVertexArray(0);
        glUseProgram(0);
    }

    ovrHmd_EndFrameTiming(hmd);
}