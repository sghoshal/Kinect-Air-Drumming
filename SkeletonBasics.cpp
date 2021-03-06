﻿//------------------------------------------------------------------------------
// <copyright file="SkeletonBasics.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#include "stdafx.h"
#include <strsafe.h>
#include "SkeletonBasics.h"
#include "resource.h"
#include <iostream>
#include <Windows.h>
#include <sstream>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

static float crash_left_xmin = -200.00,crash_left_xmax = -60.00,crash_left_ymin = 40.00,crash_left_ymax = 100.00;
static float high_hat_xmin = -30.00 ,high_hat_xmax = 60.00,high_hat_ymin = 50.00 ,high_hat_ymax = 110.00;
static float ride_xmin = 148.00 ,ride_xmax = 300.00,ride_ymin = 20.00 ,ride_ymax = 150.00;
static float snare_xmin = -40.00 ,snare_xmax = 60.00,snare_ymin = 130.00, snare_ymax = 200.00;
static float lowtom_xmin = -180.00 ,lowtom_xmax = 00.00,lowtom_ymin = 90.00, lowtom_ymax = 170.00;
static float hitom_xmin = 20.00 ,hitom_xmax = 180.00,hitom_ymin = 90.00, hitom_ymax = 170.00;
static float left_x_old = 0.00, left_y_old = 0.00, right_x_old = 0.00, right_y_old = 0.00;
static BOOL left_x_pos = false,left_y_pos = false,right_x_pos = false,right_y_pos = false;

#define DBOUT( s )          \
{                              \
	                            \
	std::wostringstream os_; \
	os_ << s;               \
	OutputDebugStringW( os_.str().c_str() ); \
}

static const float g_JointThickness = 3.0f;
static const float g_TrackedBoneThickness = 6.0f;
static const float g_InferredBoneThickness = 1.0f;

/// <summary>
/// Entry point for the application
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="hPrevInstance">always 0</param>
/// <param name="lpCmdLine">command line arguments</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
/// <returns>status</returns>
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    CSkeletonBasics application;
    application.Run(hInstance, nCmdShow);
}

/// <summary>
/// Constructor
/// </summary>
CSkeletonBasics::CSkeletonBasics() :
    m_pD2DFactory(NULL),
    m_hNextSkeletonEvent(INVALID_HANDLE_VALUE),
    m_pSkeletonStreamHandle(INVALID_HANDLE_VALUE),
    m_bSeatedMode(false),
    m_pRenderTarget(NULL),
    m_pBrushJointTracked(NULL),
    m_pBrushJointInferred(NULL),
    m_pBrushBoneTracked(NULL),
    m_pBrushBoneInferred(NULL),
    m_pNuiSensor(NULL)
{
    ZeroMemory(m_Points,sizeof(m_Points));
}

/// <summary>
/// Destructor
/// </summary>
CSkeletonBasics::~CSkeletonBasics()
{
    if (m_pNuiSensor)
    {
        m_pNuiSensor->NuiShutdown();
    }

    if (m_hNextSkeletonEvent && (m_hNextSkeletonEvent != INVALID_HANDLE_VALUE))
    {
        CloseHandle(m_hNextSkeletonEvent);
    }

    // clean up Direct2D objects
    DiscardDirect2DResources();

    // clean up Direct2D
    SafeRelease(m_pD2DFactory);

    SafeRelease(m_pNuiSensor);
}

/// <summary>
/// Creates the main window and begins processing
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
int CSkeletonBasics::Run(HINSTANCE hInstance, int nCmdShow)
{
    MSG       msg = {0};
    WNDCLASS  wc  = {0};

    // Dialog custom window class
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.cbWndExtra    = DLGWINDOWEXTRA;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
    wc.lpfnWndProc   = DefDlgProcW;
    wc.lpszClassName = L"SkeletonBasicsAppDlgWndClass";

    if (!RegisterClassW(&wc))
    {
        return 0;
    }

    // Create main application window
    HWND hWndApp = CreateDialogParamW(
        hInstance,
        MAKEINTRESOURCE(IDD_APP),
        NULL,
        (DLGPROC)CSkeletonBasics::MessageRouter, 
        reinterpret_cast<LPARAM>(this));

    // Show window
    ShowWindow(hWndApp, nCmdShow);

    const int eventCount = 1;
    HANDLE hEvents[eventCount];

    // Main message loop
    while (WM_QUIT != msg.message)
    {
        hEvents[0] = m_hNextSkeletonEvent;

        // Check to see if we have either a message (by passing in QS_ALLEVENTS)
        // Or a Kinect event (hEvents)
        // Update() will check for Kinect events individually, in case more than one are signalled
        DWORD dwEvent = MsgWaitForMultipleObjects(eventCount, hEvents, FALSE, INFINITE, QS_ALLINPUT);

        // Check if this is an event we're waiting on and not a timeout or message
        if (WAIT_OBJECT_0 == dwEvent)
        {
            Update();
        }

        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            // If a dialog message will be taken care of by the dialog proc
            if ((hWndApp != NULL) && IsDialogMessageW(hWndApp, &msg))
            {
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}

/// <summary>
/// Main processing function
/// </summary>
void CSkeletonBasics::Update()
{
    if (NULL == m_pNuiSensor)
    {
        return;
    }

    // Wait for 0ms, just quickly test if it is time to process a skeleton
    if ( WAIT_OBJECT_0 == WaitForSingleObject(m_hNextSkeletonEvent, 0) )
    {
        ProcessSkeleton();
    }
}

/// <summary>
/// Handles window messages, passes most to the class instance to handle
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CSkeletonBasics::MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CSkeletonBasics* pThis = NULL;

    if (WM_INITDIALOG == uMsg)
    {
        pThis = reinterpret_cast<CSkeletonBasics*>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<CSkeletonBasics*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis)
    {
        return pThis->DlgProc(hWnd, uMsg, wParam, lParam);
    }

    return 0;
}

/// <summary>
/// Handle windows messages for the class instance
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CSkeletonBasics::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            // Bind application window handle
            m_hWnd = hWnd;

            // Init Direct2D
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);

            // Look for a connected Kinect, and create it if found
            CreateFirstConnected();
        }
        break;

        // If the titlebar X is clicked, destroy app
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        // Quit the main message pump
        PostQuitMessage(0);
        break;

        // Handle button press
    case WM_COMMAND:
        // If it was for the near mode control and a clicked event, change near mode
        if (IDC_CHECK_SEATED == LOWORD(wParam) && BN_CLICKED == HIWORD(wParam))
        {
            // Toggle out internal state for near mode
            m_bSeatedMode = !m_bSeatedMode;

            if (NULL != m_pNuiSensor)
            {
                // Set near mode for sensor based on our internal state
                m_pNuiSensor->NuiSkeletonTrackingEnable(m_hNextSkeletonEvent, m_bSeatedMode ? NUI_SKELETON_TRACKING_FLAG_ENABLE_SEATED_SUPPORT : 0);
            }
        }
        break;
    }

    return FALSE;
}

/// <summary>
/// Create the first connected Kinect found 
/// </summary>
/// <returns>indicates success or failure</returns>
HRESULT CSkeletonBasics::CreateFirstConnected()
{
    INuiSensor * pNuiSensor;

    int iSensorCount = 0;
    HRESULT hr = NuiGetSensorCount(&iSensorCount);
    if (FAILED(hr))
    {
        return hr;
    }

    // Look at each Kinect sensor
    for (int i = 0; i < iSensorCount; ++i)
    {
        // Create the sensor so we can check status, if we can't create it, move on to the next
        hr = NuiCreateSensorByIndex(i, &pNuiSensor);
        if (FAILED(hr))
        {
            continue;
        }

        // Get the status of the sensor, and if connected, then we can initialize it
        hr = pNuiSensor->NuiStatus();
        if (S_OK == hr)
        {
            m_pNuiSensor = pNuiSensor;
            break;
        }

        // This sensor wasn't OK, so release it since we're not using it
        pNuiSensor->Release();
    }

    if (NULL != m_pNuiSensor)
    {
        // Initialize the Kinect and specify that we'll be using skeleton
        hr = m_pNuiSensor->NuiInitialize(NUI_INITIALIZE_FLAG_USES_SKELETON); 
        if (SUCCEEDED(hr))
        {
            // Create an event that will be signaled when skeleton data is available
            m_hNextSkeletonEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

            // Open a skeleton stream to receive skeleton data
            hr = m_pNuiSensor->NuiSkeletonTrackingEnable(m_hNextSkeletonEvent, 0); 
        }
    }

    if (NULL == m_pNuiSensor || FAILED(hr))
    {
        SetStatusMessage(L"No ready Kinect found!");
        return E_FAIL;
    }

    return hr;
}

/// <summary>
/// Handle new skeleton data
/// </summary>
void CSkeletonBasics::ProcessSkeleton()
{
    NUI_SKELETON_FRAME skeletonFrame = {0};

    HRESULT hr = m_pNuiSensor->NuiSkeletonGetNextFrame(0, &skeletonFrame);
    if ( FAILED(hr) )
    {
        return;
    }

    // smooth out the skeleton data
    m_pNuiSensor->NuiTransformSmooth(&skeletonFrame, NULL);

    // Endure Direct2D is ready to draw
    hr = EnsureDirect2DResources( );
    if ( FAILED(hr) )
    {
        return;
    }

    m_pRenderTarget->BeginDraw();
    m_pRenderTarget->Clear( );

    RECT rct;
    GetClientRect( GetDlgItem( m_hWnd, IDC_VIDEOVIEW ), &rct);
    int width = rct.right;
    int height = rct.bottom;

    for (int i = 0 ; i < NUI_SKELETON_COUNT; ++i)
    {
        NUI_SKELETON_TRACKING_STATE trackingState = skeletonFrame.SkeletonData[i].eTrackingState;

        if (NUI_SKELETON_TRACKED == trackingState)
        {
            // We're tracking the skeleton, draw it
            DrawSkeleton(skeletonFrame.SkeletonData[i], width, height);
        }
        else if (NUI_SKELETON_POSITION_ONLY == trackingState)
        {
            // we've only received the center point of the skeleton, draw that
            D2D1_ELLIPSE ellipse = D2D1::Ellipse(
                SkeletonToScreen(skeletonFrame.SkeletonData[i].Position, width, height),
                g_JointThickness,
                g_JointThickness
                );

            m_pRenderTarget->DrawEllipse(ellipse, m_pBrushJointTracked);
        }
    }

    hr = m_pRenderTarget->EndDraw();

    // Device lost, need to recreate the render target
    // We'll dispose it now and retry drawing
    if (D2DERR_RECREATE_TARGET == hr)
    {
        hr = S_OK;
        DiscardDirect2DResources();
    }
}

/// <summary>
/// Draws a skeleton
/// </summary>
/// <param name="skel">skeleton to draw</param>
/// <param name="windowWidth">width (in pixels) of output buffer</param>
/// <param name="windowHeight">height (in pixels) of output buffer</param>
void CSkeletonBasics::DrawSkeleton(const NUI_SKELETON_DATA & skel, int windowWidth, int windowHeight)
{      
    int i;
	LONG x,y;
		
    for (i = 0; i < NUI_SKELETON_POSITION_COUNT; ++i)
    {

        m_Points[i] = SkeletonToScreen(skel.SkeletonPositions[i], windowWidth, windowHeight);
		NuiTransformSkeletonToDepthImage(skel.SkeletonPositions[i], &x, &y, &depth[i]);

   }
	
    /* Calculating the relative depth of the left and right hand ahead of shoulder */
    USHORT rel_right_depth = depth[2] - depth[11];
	USHORT rel_left_depth = depth[2] - depth[7];

	if (rel_right_depth > 5000)
		rel_right_depth = 0;
	
    if (rel_left_depth > 5000)
		rel_left_depth = 0;

	/* Shoulder = depth[2], Left hand = depth[7], right hand = depth[11] */
	float rel_left_x,rel_left_y,rel_right_x,rel_right_y;

    /* Checking if the motion of the left hand is downward and to the right */
	if((m_Points[7].x - left_x_old) > 0)
		left_x_pos = true;
	else
		left_x_pos = false;

	if((m_Points[7].y - left_y_old) > 0)
		left_y_pos = true;
	else
		left_y_pos = false;

    /* Checking if the motion of the right hand is downward and to the left */
	if((m_Points[11].x - right_x_old) > 0)
		right_x_pos = true;
	else
		right_x_pos = false;

	if((m_Points[11].y - right_y_old) > 0)
		right_y_pos = true;
	else
		right_y_pos = false;

	left_x_old = m_Points[7].x;
	left_y_old = m_Points[7].y;
	right_x_old = m_Points[11].x;
	right_y_old = m_Points[11].y;


    /* The relative movement taking place between shoulder and left hand*/
	rel_left_x = m_Points[7].x - m_Points[2].x;
	rel_left_y = m_Points[7].y - m_Points[2].y;

    /* The relative movement taking place between shoulder and right hand*/
	rel_right_x = m_Points[11].x - m_Points[2].x;
	rel_right_y = m_Points[11].y - m_Points[2].y;

	DBOUT("LEFT (" << rel_left_x << "," << rel_left_y << ")\n");
	DBOUT("RIGHT (" << rel_right_x << "," << rel_right_y << ")\n");
	
    if (rel_left_depth > 2800 )
	{
		if ((rel_left_x < lowtom_xmax && rel_left_x > lowtom_xmin) && (rel_left_y < lowtom_ymax 
			&& rel_left_y > lowtom_ymin) && left_y_pos)
			mciSendString(TEXT("play C:\\Users\\Nirav\\Desktop\\lowTom-small.WAV"), NULL, 0, NULL);

		if ((rel_left_x < hitom_xmax && rel_left_x > hitom_xmin) && (rel_left_y < hitom_ymax 
			&& rel_left_y > hitom_ymin) && left_y_pos)
			mciSendString(TEXT("play C:\\Users\\Nirav\\Desktop\\highTom-small.WAV"), NULL, 0, NULL);
	}

	if (rel_right_depth > 2800 )
	{
		if ((rel_right_x < lowtom_xmax && rel_right_x > lowtom_xmin) && (rel_right_y < lowtom_ymax 
			&& rel_right_y > lowtom_ymin) && right_y_pos)
			mciSendString(TEXT("play C:\\Users\\Nirav\\Desktop\\lowTom-small.WAV"), NULL, 0, NULL);

		if ((rel_right_x < hitom_xmax && rel_right_x > hitom_xmin) && (rel_right_y < hitom_ymax 
			&& rel_right_y > hitom_ymin) && right_y_pos)
			mciSendString(TEXT("play C:\\Users\\Nirav\\Desktop\\highTom-small.WAV"), NULL, 0, NULL);
	}


	if ((rel_left_x < snare_xmax && rel_left_x > snare_xmin) && (rel_left_y < snare_ymax && rel_left_y > snare_ymin) && 
        left_y_pos && rel_left_depth <= 2500)
	{
		DBOUT("Snare played \n");
		//PlaySound(TEXT("C:\\Users\\Nirav\\Desktop\\snareDrum.WAV"), NULL, SND_ASYNC);
		mciSendString(TEXT("play C:\\Users\\Nirav\\Desktop\\snareDrum.WAV"), NULL, 0, NULL);
		
	}

	if ((rel_right_x < ride_xmax && rel_right_x > ride_xmin) && (rel_right_y < ride_ymax && rel_right_y > ride_ymin) 
		&& right_x_pos && rel_right_depth <=2500)
	{
		DBOUT("Ride played \n");
		mciSendString(TEXT("play C:\\Users\\Nirav\\Desktop\\ride-small.WAV"), NULL, 0, NULL);
	}

	if ((rel_right_x < high_hat_xmax && rel_right_x > high_hat_xmin) && (rel_right_y < high_hat_ymax && rel_right_y > high_hat_ymin)
		&& right_y_pos && rel_right_depth <= 2500)
	{
		DBOUT("High Hat played \n");
		DBOUT("("<< rel_right_x << ","<< rel_right_y<<") \n");
		//PlaySound(TEXT("C:\\Users\\Nirav\\Desktop\\hihatDrum.WAV"), NULL, SND_ASYNC);
		mciSendString(TEXT("play C:\\Users\\Nirav\\Desktop\\hihatDrum.WAV"), NULL, 0, NULL);
	}
	
	if ((rel_right_x < crash_left_xmax && rel_right_x > crash_left_xmin) && 
		(rel_right_y < crash_left_ymax && rel_right_y > crash_left_ymin) && right_y_pos && rel_right_depth <= 2500)
	{
		DBOUT("Crash Left played \n");
		//PlaySound(TEXT("C:\\Users\\Nirav\\Desktop\\leftCrashDrum-small.WAV"), NULL, SND_ASYNC);
		mciSendString(TEXT("play C:\\Users\\Nirav\\Desktop\\leftCrashDrum-small.WAV"), NULL, 0, NULL);
	}

	/* Draw Crash */
	D2D1_RECT_F crash_shape;
	crash_shape.left = m_Points[2].x + crash_left_xmin;
	crash_shape.right = m_Points[2].x + crash_left_xmax;
	crash_shape.top = m_Points[2].y + crash_left_ymin;
	crash_shape.bottom = m_Points[2].y + crash_left_ymax;
	m_pRenderTarget->DrawRectangle(crash_shape, m_pShape, g_TrackedBoneThickness - 5.0);

	/* Draw Hihat */
	D2D1_RECT_F hihat_shape;
	hihat_shape.left = m_Points[2].x + high_hat_xmin;
	hihat_shape.right = m_Points[2].x + high_hat_xmax;
	hihat_shape.top = m_Points[2].y + high_hat_ymin;
	hihat_shape.bottom = m_Points[2].y + high_hat_ymax;
	m_pRenderTarget->DrawRectangle(hihat_shape, m_pShape, g_TrackedBoneThickness - 5.0);

	/* Draw Snare */
	D2D1_RECT_F snare_shape;
	snare_shape.left = m_Points[2].x + snare_xmin;
	snare_shape.right = m_Points[2].x + snare_xmax;
	snare_shape.top = m_Points[2].y + snare_ymin;
	snare_shape.bottom = m_Points[2].y + snare_ymax;
	m_pRenderTarget->DrawRectangle(snare_shape, m_pShape, g_TrackedBoneThickness - 5.0);

	/* Draw Ride */
	D2D1_RECT_F ride_shape;
	ride_shape.left = m_Points[2].x + ride_xmin;
	ride_shape.right = m_Points[2].x + ride_xmax;
	ride_shape.top = m_Points[2].y + ride_ymin;
	ride_shape.bottom = m_Points[2].y + ride_ymax;
	m_pRenderTarget->DrawRectangle(ride_shape, m_pShape, g_TrackedBoneThickness - 5.0);

	/* Draw HiTom */
	D2D1_RECT_F hitom_shape;
	hitom_shape.left = m_Points[2].x + hitom_xmin;
	hitom_shape.right = m_Points[2].x + hitom_xmax;
	hitom_shape.top = m_Points[2].y + hitom_ymin;
	hitom_shape.bottom = m_Points[2].y + hitom_ymax;
	m_pRenderTarget->DrawRectangle(hitom_shape, m_pBrushJointInferred, g_TrackedBoneThickness - 5.0);

	/* Draw LowTom */
	D2D1_RECT_F lowtom_shape;
	lowtom_shape.left = m_Points[2].x + lowtom_xmin;
	lowtom_shape.right = m_Points[2].x + lowtom_xmax;
	lowtom_shape.top = m_Points[2].y + lowtom_ymin;
	lowtom_shape.bottom = m_Points[2].y + lowtom_ymax;
	m_pRenderTarget->DrawRectangle(lowtom_shape, m_pBrushJointInferred, g_TrackedBoneThickness - 5.0);


    // Render Torso
    DrawBone(skel, NUI_SKELETON_POSITION_HEAD, NUI_SKELETON_POSITION_SHOULDER_CENTER);
    DrawBone(skel, NUI_SKELETON_POSITION_SHOULDER_CENTER, NUI_SKELETON_POSITION_SHOULDER_LEFT);
    DrawBone(skel, NUI_SKELETON_POSITION_SHOULDER_CENTER, NUI_SKELETON_POSITION_SHOULDER_RIGHT);
    DrawBone(skel, NUI_SKELETON_POSITION_SHOULDER_CENTER, NUI_SKELETON_POSITION_SPINE);
    DrawBone(skel, NUI_SKELETON_POSITION_SPINE, NUI_SKELETON_POSITION_HIP_CENTER);
    DrawBone(skel, NUI_SKELETON_POSITION_HIP_CENTER, NUI_SKELETON_POSITION_HIP_LEFT);
    DrawBone(skel, NUI_SKELETON_POSITION_HIP_CENTER, NUI_SKELETON_POSITION_HIP_RIGHT);

    // Left Arm
    DrawBone(skel, NUI_SKELETON_POSITION_SHOULDER_LEFT, NUI_SKELETON_POSITION_ELBOW_LEFT);
    DrawBone(skel, NUI_SKELETON_POSITION_ELBOW_LEFT, NUI_SKELETON_POSITION_WRIST_LEFT);
    DrawBone(skel, NUI_SKELETON_POSITION_WRIST_LEFT, NUI_SKELETON_POSITION_HAND_LEFT);

    // Right Arm
    DrawBone(skel, NUI_SKELETON_POSITION_SHOULDER_RIGHT, NUI_SKELETON_POSITION_ELBOW_RIGHT);
    DrawBone(skel, NUI_SKELETON_POSITION_ELBOW_RIGHT, NUI_SKELETON_POSITION_WRIST_RIGHT);
    DrawBone(skel, NUI_SKELETON_POSITION_WRIST_RIGHT, NUI_SKELETON_POSITION_HAND_RIGHT);

    // Left Leg
    DrawBone(skel, NUI_SKELETON_POSITION_HIP_LEFT, NUI_SKELETON_POSITION_KNEE_LEFT);
    DrawBone(skel, NUI_SKELETON_POSITION_KNEE_LEFT, NUI_SKELETON_POSITION_ANKLE_LEFT);
    DrawBone(skel, NUI_SKELETON_POSITION_ANKLE_LEFT, NUI_SKELETON_POSITION_FOOT_LEFT);

    // Right Leg
    DrawBone(skel, NUI_SKELETON_POSITION_HIP_RIGHT, NUI_SKELETON_POSITION_KNEE_RIGHT);
    DrawBone(skel, NUI_SKELETON_POSITION_KNEE_RIGHT, NUI_SKELETON_POSITION_ANKLE_RIGHT);
    DrawBone(skel, NUI_SKELETON_POSITION_ANKLE_RIGHT, NUI_SKELETON_POSITION_FOOT_RIGHT);

    // Draw the joints in a different color
    for (i = 0; i < NUI_SKELETON_POSITION_COUNT; ++i)
    {
        D2D1_ELLIPSE ellipse = D2D1::Ellipse( m_Points[i], g_JointThickness, g_JointThickness );

        if ( skel.eSkeletonPositionTrackingState[i] == NUI_SKELETON_POSITION_INFERRED )
        {
            m_pRenderTarget->DrawEllipse(ellipse, m_pBrushJointInferred);
        }
        else if ( skel.eSkeletonPositionTrackingState[i] == NUI_SKELETON_POSITION_TRACKED )
        {
            m_pRenderTarget->DrawEllipse(ellipse, m_pBrushJointTracked);
        }
    }
}

/// <summary>
/// Draws a bone line between two joints
/// </summary>
/// <param name="skel">skeleton to draw bones from</param>
/// <param name="joint0">joint to start drawing from</param>
/// <param name="joint1">joint to end drawing at</param>
void CSkeletonBasics::DrawBone(const NUI_SKELETON_DATA & skel, NUI_SKELETON_POSITION_INDEX joint0, NUI_SKELETON_POSITION_INDEX joint1)
{
    NUI_SKELETON_POSITION_TRACKING_STATE joint0State = skel.eSkeletonPositionTrackingState[joint0];
    NUI_SKELETON_POSITION_TRACKING_STATE joint1State = skel.eSkeletonPositionTrackingState[joint1];

	


    // If we can't find either of these joints, exit
    if (joint0State == NUI_SKELETON_POSITION_NOT_TRACKED || joint1State == NUI_SKELETON_POSITION_NOT_TRACKED)
    {
        return;
    }

    // Don't draw if both points are inferred
    if (joint0State == NUI_SKELETON_POSITION_INFERRED && joint1State == NUI_SKELETON_POSITION_INFERRED)
    {
        return;
    }

    // We assume all drawn bones are inferred unless BOTH joints are tracked
    if (joint0State == NUI_SKELETON_POSITION_TRACKED && joint1State == NUI_SKELETON_POSITION_TRACKED)
    {
        m_pRenderTarget->DrawLine(m_Points[joint0], m_Points[joint1], m_pBrushBoneTracked, g_TrackedBoneThickness);
    }
    else
    {
        m_pRenderTarget->DrawLine(m_Points[joint0], m_Points[joint1], m_pBrushBoneInferred, g_InferredBoneThickness);
    }
}

/// <summary>
/// Converts a skeleton point to screen space
/// </summary>
/// <param name="skeletonPoint">skeleton point to tranform</param>
/// <param name="width">width (in pixels) of output buffer</param>
/// <param name="height">height (in pixels) of output buffer</param>
/// <returns>point in screen-space</returns>
D2D1_POINT_2F CSkeletonBasics::SkeletonToScreen(Vector4 skeletonPoint, int width, int height)
{
    LONG x, y;
    USHORT depth;

    // Calculate the skeleton's position on the screen
    // NuiTransformSkeletonToDepthImage returns coordinates in NUI_IMAGE_RESOLUTION_320x240 space
    NuiTransformSkeletonToDepthImage(skeletonPoint, &x, &y, &depth);

    float screenPointX = static_cast<float>(x * width) / cScreenWidth;
    float screenPointY = static_cast<float>(y * height) / cScreenHeight;

    return D2D1::Point2F(screenPointX, screenPointY);
}

/// <summary>
/// Ensure necessary Direct2d resources are created
/// </summary>
/// <returns>S_OK if successful, otherwise an error code</returns>
HRESULT CSkeletonBasics::EnsureDirect2DResources()
{
    HRESULT hr = S_OK;

    // If there isn't currently a render target, we need to create one
    if (NULL == m_pRenderTarget)
    {
        RECT rc;
        GetWindowRect( GetDlgItem( m_hWnd, IDC_VIDEOVIEW ), &rc );  

        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        D2D1_SIZE_U size = D2D1::SizeU( width, height );
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
        rtProps.pixelFormat = D2D1::PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
        rtProps.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;

        // Create a Hwnd render target, in order to render to the window set in initialize
        hr = m_pD2DFactory->CreateHwndRenderTarget(
            rtProps,
            D2D1::HwndRenderTargetProperties(GetDlgItem( m_hWnd, IDC_VIDEOVIEW), size),
            &m_pRenderTarget
            );
        if ( FAILED(hr) )
        {
            SetStatusMessage(L"Couldn't create Direct2D render target!");
            return hr;
        }

        //light green
        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.27f, 0.75f, 0.27f), &m_pBrushJointTracked);

        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Yellow, 1.0f), &m_pBrushJointInferred);
        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Green, 1.0f), &m_pBrushBoneTracked);
        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray, 1.0f), &m_pBrushBoneInferred);
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red, 1.0f), &m_pShape);
    }

    return hr;
}

/// <summary>
/// Dispose Direct2d resources 
/// </summary>
void CSkeletonBasics::DiscardDirect2DResources( )
{
    SafeRelease(m_pRenderTarget);

    SafeRelease(m_pBrushJointTracked);
    SafeRelease(m_pBrushJointInferred);
    SafeRelease(m_pBrushBoneTracked);
    SafeRelease(m_pBrushBoneInferred);
}

/// <summary>
/// Set the status bar message
/// </summary>
/// <param name="szMessage">message to display</param>
void CSkeletonBasics::SetStatusMessage(WCHAR * szMessage)
{
    SendDlgItemMessageW(m_hWnd, IDC_STATUS, WM_SETTEXT, 0, (LPARAM)szMessage);
}