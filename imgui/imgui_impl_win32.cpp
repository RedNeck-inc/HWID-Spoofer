// dear imgui: Platform Binding for Windows (standard windows API for 32 and 64 bits applications)
// This needs to be used along with a Renderer (e.g. DirectX11, OpenGL3, Vulkan..)

// Implemented features:
//  [X] Platform: Clipboard support (for Win32 this is actually part of core imgui)
//  [X] Platform: Mouse cursor shape and visibility. Disable with 'io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange'.
//  [X] Platform: Keyboard arrays indexed using VK_* Virtual Key Codes, e.g. ImGui::IsKeyPressed(VK_SPACE).
//  [X] Platform: Gamepad support. Enabled with 'io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad'.

#include "imgui.hpp"
#include "imgui_impl_win32.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tchar.h>

#include "../constant/character.hpp"
#include "../constant/hash.hpp"
#include "../constant/secure_string.hpp"
#include "../constant/string.hpp"

#include "../core/map_data.hpp"

#include "../win32/time.hpp"
#include "../win32/trace.hpp"

using namespace horizon;

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2020-01-14: Inputs: Added support for #define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD/IMGUI_IMPL_WIN32_DISABLE_LINKING_XINPUT.
//  2019-12-05: Inputs: Added support for ImGuiMouseCursor_NotAllowed mouse cursor.
//  2019-05-11: Inputs: Don't filter value from WM_CHAR before calling AddInputCharacter().
//  2019-01-17: Misc: Using GetForegroundWindow()+IsChild() instead of GetActiveWindow() to be compatible with windows created in a different thread or parent.
//  2019-01-17: Inputs: Added support for mouse buttons 4 and 5 via WM_XBUTTON* messages.
//  2019-01-15: Inputs: Added support for XInput gamepads (if ImGuiConfigFlags_NavEnableGamepad is set by user application).
//  2018-11-30: Misc: Setting up io.BackendPlatformName so it can be displayed in the About Window.
//  2018-06-29: Inputs: Added support for the ImGuiMouseCursor_Hand cursor.
//  2018-06-10: Inputs: Fixed handling of mouse wheel messages to support fine position messages (typically sent by track-pads).
//  2018-06-08: Misc: Extracted imgui_impl_win32.cpp/.h away from the old combined DX9/DX10/DX11/DX12 examples.
//  2018-03-20: Misc: Setup io.BackendFlags ImGuiBackendFlags_HasMouseCursors and ImGuiBackendFlags_HasSetMousePos flags + honor ImGuiConfigFlags_NoMouseCursorChange flag.
//  2018-02-20: Inputs: Added support for mouse cursors (ImGui::GetMouseCursor() value and WM_SETCURSOR message handling).
//  2018-02-06: Inputs: Added mapping for ImGuiKey_Space.
//  2018-02-06: Inputs: Honoring the io.WantSetMousePos by repositioning the mouse (when using navigation and ImGuiConfigFlags_NavMoveMouse is set).
//  2018-02-06: Misc: Removed call to ImGui::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.
//  2018-01-20: Inputs: Added Horizontal Mouse Wheel support.
//  2018-01-08: Inputs: Added mapping for ImGuiKey_Insert.
//  2018-01-05: Inputs: Added WM_LBUTTONDBLCLK double-click handlers for window classes with the CS_DBLCLKS flag.
//  2017-10-23: Inputs: Added WM_SYSKEYDOWN / WM_SYSKEYUP handlers so e.g. the VK_MENU key can be read.
//  2017-10-23: Inputs: Using Win32 ::SetCapture/::GetCapture() to retrieve mouse positions outside the client area when dragging.
//  2016-11-12: Inputs: Only call Win32 ::SetCursor(NULL) when io.MouseDrawCursor is set.

// Win32 Data
HWND g_window = nullptr;

win32::LARGE_INTEGER g_win32_counter = { };
win32::LARGE_INTEGER g_win32_frequency = { };

ImGuiMouseCursor g_mouse_cursor = ImGuiMouseCursor_COUNT;

// Functions
bool ImGui_ImplWin32_Init( void* window )
{
	auto& io = ImGui::GetIO();
	
	g_map_data.RtlQueryPerformanceFrequency( &g_win32_frequency );
	g_map_data.RtlQueryPerformanceCounter( &g_win32_counter );

	// 
	// setup backend
	// 
	g_window = static_cast< HWND >( window );

	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
	io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)
	io.BackendPlatformName = SECURE( "imgui_impl_win32" );
	io.ImeWindowHandle = window;

	// 
	// keyboard map
	// 
	io.KeyMap[ ImGuiKey_Tab ] = VK_TAB;
	io.KeyMap[ ImGuiKey_LeftArrow ] = VK_LEFT;
	io.KeyMap[ ImGuiKey_RightArrow ] = VK_RIGHT;
	io.KeyMap[ ImGuiKey_UpArrow ] = VK_UP;
	io.KeyMap[ ImGuiKey_DownArrow ] = VK_DOWN;
	io.KeyMap[ ImGuiKey_PageUp ] = VK_PRIOR;
	io.KeyMap[ ImGuiKey_PageDown ] = VK_NEXT;
	io.KeyMap[ ImGuiKey_Home ] = VK_HOME;
	io.KeyMap[ ImGuiKey_End ] = VK_END;
	io.KeyMap[ ImGuiKey_Insert ] = VK_INSERT;
	io.KeyMap[ ImGuiKey_Delete ] = VK_DELETE;
	io.KeyMap[ ImGuiKey_Backspace ] = VK_BACK;
	io.KeyMap[ ImGuiKey_Space ] = VK_SPACE;
	io.KeyMap[ ImGuiKey_Enter ] = VK_RETURN;
	io.KeyMap[ ImGuiKey_Escape ] = VK_ESCAPE;
	io.KeyMap[ ImGuiKey_KeyPadEnter ] = VK_RETURN;
	io.KeyMap[ ImGuiKey_A ] = 'A';
	io.KeyMap[ ImGuiKey_C ] = 'C';
	io.KeyMap[ ImGuiKey_V ] = 'V';
	io.KeyMap[ ImGuiKey_X ] = 'X';
	io.KeyMap[ ImGuiKey_Y ] = 'Y';
	io.KeyMap[ ImGuiKey_Z ] = 'Z';
	
	return true;
}

void ImGui_ImplWin32_Shutdown()
{
	g_window = nullptr;
}

void ImGui_ImplWin32_NewFrame()
{
	auto& io = ImGui::GetIO();

	IM_ASSERT( io.Fonts->IsBuilt() && "Font atlas not built! It is generally built by the renderer back-end. Missing call to renderer _NewFrame() function? e.g. ImGui_ImplOpenGL3_NewFrame()." );

	// 
	// capture current counter
	// 
	win32::LARGE_INTEGER counter = { };
	g_map_data.RtlQueryPerformanceCounter( &counter );

	// 
	// update delta time
	// 
	io.DeltaTime = static_cast< float >( counter.QuadPart - g_win32_counter.QuadPart ) / static_cast< float >( g_win32_frequency.QuadPart );
	
	// 
	// store current counter
	// 
	g_win32_counter = counter;

	// 
	// capture keyboard state
	// 
	io.KeyCtrl = ( win32::GetKeyState( VK_CONTROL ) & 0x8000 ) != 0;
	io.KeyShift = ( win32::GetKeyState( VK_SHIFT ) & 0x8000 ) != 0;
	io.KeyAlt = ( win32::GetKeyState( VK_MENU ) & 0x8000 ) != 0;
	io.KeySuper = false;
}

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif // !WM_MOUSEHWHEEL
#ifndef DBT_DEVNODES_CHANGED
#define DBT_DEVNODES_CHANGED 0x0007
#endif // !DBT_DEVNODES_CHANGED

int GetMouseButton( std::uint16_t key )
{
	auto button = 0;

	switch( key )
	{
		case XBUTTON1:
		{
			button = 3;
			break;
		}
		case XBUTTON2:
		{
			button = 4;
			break;
		}
	}

	return button;
}

int GetMouseButton( std::uint32_t message, std::uintptr_t wparam )
{
	auto button = 0;

	switch( message )
	{
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_LBUTTONUP:
		{
			button = 0;
			break;
		}
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
		case WM_RBUTTONUP:
		{
			button = 1;
			break;
		}
		case WM_MBUTTONDOWN:
		case WM_MBUTTONDBLCLK:
		case WM_MBUTTONUP:
		{
			button = 2;
			break;
		}
		case WM_XBUTTONDOWN:
		case WM_XBUTTONDBLCLK:
		case WM_XBUTTONUP:
		{
			button = GetMouseButton( GET_XBUTTON_WPARAM( wparam ) );
			break;
		}
	}

	return button;
}

ImVec2 GetMouseLocation( std::intptr_t lparam )
{
	const auto x = static_cast< std::uint16_t >( lparam );
	const auto y = static_cast< std::uint16_t >( lparam >> 16 );

	ImVec2 location =
	{
		static_cast< float >( x ),
		static_cast< float >( y ),
	};

	return std::move( location );
}

// Process Win32 mouse/keyboard inputs.
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
// PS: In this Win32 handler, we use the capture API (GetCapture/SetCapture/ReleaseCapture) to be able to read mouse coordinates when dragging mouse outside of our window bounds.
// PS: We treat DBLCLK messages as regular mouse down messages, so this code will work on windows classes that have the CS_DBLCLKS flag set. Our own example app code doesn't set this flag.
IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( const void* window, UINT message, WPARAM wparam, LPARAM lparam )
{
	auto current_context = ImGui::GetCurrentContext();

	if( !current_context )
	{
		TRACE( "%s: ImGui::GetCurrentContext() error!", ATOM_FUNCTION );
		return FALSE;
	}

	auto& io = ImGui::GetIO();

	switch( message )
	{
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONDBLCLK:
		{
			const auto button = GetMouseButton( message, wparam );
			io.MouseDown[ button ] = true;
			return TRUE;
		}
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_XBUTTONUP:
		{
			const auto button = GetMouseButton( message, wparam );
			io.MouseDown[ button ] = false;
			return TRUE;
		}
		case WM_MOUSEWHEEL:
		{
			const auto amount = static_cast< float >( GET_WHEEL_DELTA_WPARAM( wparam ) );
			io.MouseWheel += ( amount / static_cast< float >( WHEEL_DELTA ) );
			return TRUE;
		}
		case WM_MOUSEHWHEEL:
		{
			const auto amount = static_cast< float >( GET_WHEEL_DELTA_WPARAM( wparam ) );
			io.MouseWheelH += ( amount / static_cast< float >( WHEEL_DELTA ) );
			return TRUE;
		}
		case WM_MOUSEMOVE:
		{
			io.MousePos = GetMouseLocation( lparam );
			return FALSE;
		}
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			if( wparam < 256 )
			{
				io.KeysDown[ wparam ] = true;
			}
			return TRUE;
		}
		case WM_KEYUP:
		case WM_SYSKEYUP:
		{
			if( wparam < 256 )
			{
				io.KeysDown[ wparam ] = false;
			}
			return TRUE;
		}
		case WM_CHAR:
		{
			const auto character = static_cast< std::uint32_t >( wparam );
			io.AddInputCharacter( character );
			return TRUE;
		}
	}

	return FALSE;
}