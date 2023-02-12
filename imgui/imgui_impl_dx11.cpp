// dear imgui: Renderer for DirectX11
// This needs to be used along with a Platform Binding (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID3D11ShaderResourceView*' as ImTextureID. Read the FAQ about ImTextureID!
//  [X] Renderer: Support for large meshes (64k+ vertices) with 16-bit indices.

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you are new to dear imgui, read examples/README.txt and read the documentation at the top of imgui.cpp
// https://github.com/ocornut/imgui

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2019-08-01: DirectX11: Fixed code querying the Geometry Shader state (would generally error with Debug layer enabled).
//  2019-07-21: DirectX11: Backup, clear and restore Geometry Shader is any is bound when calling ImGui_ImplDX10_RenderDrawData. Clearing Hull/Domain/Compute shaders without backup/restore.
//  2019-05-29: DirectX11: Added support for large mesh (64K+ vertices), enable ImGuiBackendFlags_RendererHasVtxOffset flag.
//  2019-04-30: DirectX11: Added support for special ImDrawCallback_ResetRenderState callback to reset render state.
//  2018-12-03: Misc: Added #pragma comment statement to automatically link with d3dcompiler.lib when using D3DCompile().
//  2018-11-30: Misc: Setting up io.BackendRendererName so it can be displayed in the About Window.
//  2018-08-01: DirectX11: Querying for IDXGIFactory instead of IDXGIFactory1 to increase compatibility.
//  2018-07-13: DirectX11: Fixed unreleased resources in Init and Shutdown functions.
//  2018-06-08: Misc: Extracted imgui_impl_dx11.cpp/.h away from the old combined DX11+Win32 example.
//  2018-06-08: DirectX11: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle.
//  2018-02-16: Misc: Obsoleted the io.RenderDrawListsFn callback and exposed ImGui_ImplDX11_RenderDrawData() in the .h file so you can call it yourself.
//  2018-02-06: Misc: Removed call to ImGui::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.
//  2016-05-07: DirectX11: Disabling depth-write.

#include "imgui.hpp"
#include "imgui_impl_dx11.hpp"
#include "imgui_shader_dx11.hpp"

#include "../constant/character.hpp"
#include "../constant/hash.hpp"
#include "../constant/secure_string.hpp"
#include "../constant/string.hpp"

#include "../core/map_data.hpp"

#include "../memory/operation.hpp"

#include "../win32/time.hpp"
#include "../win32/trace.hpp"

using namespace horizon;

// DirectX
#include <stdio.h>
#include <d3d11.h>

// DirectX data
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGIFactory* g_pFactory = nullptr;
ID3D11Buffer* g_pVB = nullptr;
ID3D11Buffer* g_pIB = nullptr;
ID3D10Blob* g_pVertexShaderBlob = nullptr;
ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;
ID3D11Buffer* g_pVertexConstantBuffer = nullptr;
ID3D10Blob* g_pPixelShaderBlob = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11SamplerState* g_pFontSampler = nullptr;
ID3D11ShaderResourceView* g_pFontTextureView = nullptr;
ID3D11RasterizerState* g_pRasterizerState = nullptr;
ID3D11BlendState* g_pBlendState = nullptr;
ID3D11DepthStencilState* g_pDepthStencilState = nullptr;

int g_VertexBufferSize = 5000;
int g_IndexBufferSize = 10000;

win32::LARGE_INTEGER g_dx11_counter = { };
win32::LARGE_INTEGER g_dx11_frequency = { };

struct VERTEX_CONSTANT_BUFFER
{
	float mvp[ 4 ][ 4 ] = { };
};

void ImGui_ImplDX11_SetupRenderState( ImDrawData* draw_data, ID3D11DeviceContext* ctx )
{
	// Setup viewport
	D3D11_VIEWPORT vp;
	memset( &vp, 0, sizeof( D3D11_VIEWPORT ) );
	vp.Width = draw_data->DisplaySize.x;
	vp.Height = draw_data->DisplaySize.y;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = vp.TopLeftY = 0;
	ctx->RSSetViewports( 1, &vp );

	// Setup shader and vertex buffers
	unsigned int stride = sizeof( ImDrawVert );
	unsigned int offset = 0;
	ctx->IASetInputLayout( g_pInputLayout );
	ctx->IASetVertexBuffers( 0, 1, &g_pVB, &stride, &offset );
	ctx->IASetIndexBuffer( g_pIB, sizeof( ImDrawIdx ) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0 );
	ctx->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	ctx->VSSetShader( g_pVertexShader, NULL, 0 );
	ctx->VSSetConstantBuffers( 0, 1, &g_pVertexConstantBuffer );
	ctx->PSSetShader( g_pPixelShader, NULL, 0 );
	ctx->PSSetSamplers( 0, 1, &g_pFontSampler );
	ctx->GSSetShader( NULL, NULL, 0 );
	ctx->HSSetShader( NULL, NULL, 0 ); // In theory we should backup and restore this as well.. very infrequently used..
	ctx->DSSetShader( NULL, NULL, 0 ); // In theory we should backup and restore this as well.. very infrequently used..
	ctx->CSSetShader( NULL, NULL, 0 ); // In theory we should backup and restore this as well.. very infrequently used..

	// Setup blend state
	const float blend_factor[ 4 ] = { 0.f, 0.f, 0.f, 0.f };
	ctx->OMSetBlendState( g_pBlendState, blend_factor, 0xffffffff );
	ctx->OMSetDepthStencilState( g_pDepthStencilState, 0 );
	ctx->RSSetState( g_pRasterizerState );
}

// Render function
// (this used to be set in io.RenderDrawListsFn and called by ImGui::Render(), but you can now call this directly from your main loop)
void ImGui_ImplDX11_RenderDrawData( ImDrawData* draw_data )
{
	// Avoid rendering when minimized
	if( draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f )
		return;

	ID3D11DeviceContext* ctx = g_pd3dDeviceContext;

	// Create and grow vertex/index buffers if needed
	if( !g_pVB || g_VertexBufferSize < draw_data->TotalVtxCount )
	{
		if( g_pVB )
		{
			g_pVB->Release(); g_pVB = NULL;
		}
		g_VertexBufferSize = draw_data->TotalVtxCount + 5000;
		D3D11_BUFFER_DESC desc;
		memset( &desc, 0, sizeof( D3D11_BUFFER_DESC ) );
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.ByteWidth = g_VertexBufferSize * sizeof( ImDrawVert );
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		if( g_pd3dDevice->CreateBuffer( &desc, NULL, &g_pVB ) < 0 )
			return;
	}
	if( !g_pIB || g_IndexBufferSize < draw_data->TotalIdxCount )
	{
		if( g_pIB )
		{
			g_pIB->Release(); g_pIB = NULL;
		}
		g_IndexBufferSize = draw_data->TotalIdxCount + 10000;
		D3D11_BUFFER_DESC desc;
		memset( &desc, 0, sizeof( D3D11_BUFFER_DESC ) );
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.ByteWidth = g_IndexBufferSize * sizeof( ImDrawIdx );
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if( g_pd3dDevice->CreateBuffer( &desc, NULL, &g_pIB ) < 0 )
			return;
	}

	// Upload vertex/index data into a single contiguous GPU buffer
	D3D11_MAPPED_SUBRESOURCE vtx_resource, idx_resource;
	if( ctx->Map( g_pVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_resource ) != S_OK )
		return;
	if( ctx->Map( g_pIB, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_resource ) != S_OK )
		return;
	ImDrawVert* vtx_dst = ( ImDrawVert* )vtx_resource.pData;
	ImDrawIdx* idx_dst = ( ImDrawIdx* )idx_resource.pData;
	for( int n = 0; n < draw_data->CmdListsCount; n++ )
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[ n ];
		memcpy( vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof( ImDrawVert ) );
		memcpy( idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof( ImDrawIdx ) );
		vtx_dst += cmd_list->VtxBuffer.Size;
		idx_dst += cmd_list->IdxBuffer.Size;
	}
	ctx->Unmap( g_pVB, 0 );
	ctx->Unmap( g_pIB, 0 );

	// Setup orthographic projection matrix into our constant buffer
	// Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
	{
		D3D11_MAPPED_SUBRESOURCE mapped_resource;
		if( ctx->Map( g_pVertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource ) != S_OK )
			return;
		VERTEX_CONSTANT_BUFFER* constant_buffer = ( VERTEX_CONSTANT_BUFFER* )mapped_resource.pData;
		float L = draw_data->DisplayPos.x;
		float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
		float T = draw_data->DisplayPos.y;
		float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
		float mvp[ 4 ][ 4 ] =
		{
				{ 2.0f / ( R - L ),   0.0f,           0.0f,       0.0f },
				{ 0.0f,         2.0f / ( T - B ),     0.0f,       0.0f },
				{ 0.0f,         0.0f,           0.5f,       0.0f },
				{ ( R + L ) / ( L - R ),  ( T + B ) / ( B - T ),    0.5f,       1.0f },
		};
		memcpy( &constant_buffer->mvp, mvp, sizeof( mvp ) );
		ctx->Unmap( g_pVertexConstantBuffer, 0 );
	}

	// Backup DX state that will be modified to restore it afterwards (unfortunately this is very ugly looking and verbose. Close your eyes!)
	struct BACKUP_DX11_STATE
	{
		UINT                        ScissorRectsCount, ViewportsCount;
		D3D11_RECT                  ScissorRects[ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE ];
		D3D11_VIEWPORT              Viewports[ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE ];
		ID3D11RasterizerState* RS;
		ID3D11BlendState* BlendState;
		FLOAT                       BlendFactor[ 4 ];
		UINT                        SampleMask;
		UINT                        StencilRef;
		ID3D11DepthStencilState* DepthStencilState;
		ID3D11ShaderResourceView* PSShaderResource;
		ID3D11SamplerState* PSSampler;
		ID3D11PixelShader* PS;
		ID3D11VertexShader* VS;
		ID3D11GeometryShader* GS;
		UINT                        PSInstancesCount, VSInstancesCount, GSInstancesCount;
		ID3D11ClassInstance* PSInstances[ 256 ], * VSInstances[ 256 ], * GSInstances[ 256 ];   // 256 is max according to PSSetShader documentation
		D3D11_PRIMITIVE_TOPOLOGY    PrimitiveTopology;
		ID3D11Buffer* IndexBuffer, * VertexBuffer, * VSConstantBuffer;
		UINT                        IndexBufferOffset, VertexBufferStride, VertexBufferOffset;
		DXGI_FORMAT                 IndexBufferFormat;
		ID3D11InputLayout* InputLayout;
	};
	BACKUP_DX11_STATE old;
	old.ScissorRectsCount = old.ViewportsCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	ctx->RSGetScissorRects( &old.ScissorRectsCount, old.ScissorRects );
	ctx->RSGetViewports( &old.ViewportsCount, old.Viewports );
	ctx->RSGetState( &old.RS );
	ctx->OMGetBlendState( &old.BlendState, old.BlendFactor, &old.SampleMask );
	ctx->OMGetDepthStencilState( &old.DepthStencilState, &old.StencilRef );
	ctx->PSGetShaderResources( 0, 1, &old.PSShaderResource );
	ctx->PSGetSamplers( 0, 1, &old.PSSampler );
	old.PSInstancesCount = old.VSInstancesCount = old.GSInstancesCount = 256;
	ctx->PSGetShader( &old.PS, old.PSInstances, &old.PSInstancesCount );
	ctx->VSGetShader( &old.VS, old.VSInstances, &old.VSInstancesCount );
	ctx->VSGetConstantBuffers( 0, 1, &old.VSConstantBuffer );
	ctx->GSGetShader( &old.GS, old.GSInstances, &old.GSInstancesCount );

	ctx->IAGetPrimitiveTopology( &old.PrimitiveTopology );
	ctx->IAGetIndexBuffer( &old.IndexBuffer, &old.IndexBufferFormat, &old.IndexBufferOffset );
	ctx->IAGetVertexBuffers( 0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset );
	ctx->IAGetInputLayout( &old.InputLayout );

	// Setup desired DX state
	ImGui_ImplDX11_SetupRenderState( draw_data, ctx );

	// Render command lists
	// (Because we merged all buffers into a single one, we maintain our own offset into them)
	int global_idx_offset = 0;
	int global_vtx_offset = 0;
	ImVec2 clip_off = draw_data->DisplayPos;
	for( int n = 0; n < draw_data->CmdListsCount; n++ )
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[ n ];
		for( int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++ )
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[ cmd_i ];
			if( pcmd->UserCallback != NULL )
			{
				// User callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
				if( pcmd->UserCallback == ImDrawCallback_ResetRenderState )
					ImGui_ImplDX11_SetupRenderState( draw_data, ctx );
				else
					pcmd->UserCallback( cmd_list, pcmd );
			}
			else
			{
				// Apply scissor/clipping rectangle
				const D3D11_RECT r = { ( LONG )( pcmd->ClipRect.x - clip_off.x ), ( LONG )( pcmd->ClipRect.y - clip_off.y ), ( LONG )( pcmd->ClipRect.z - clip_off.x ), ( LONG )( pcmd->ClipRect.w - clip_off.y ) };
				ctx->RSSetScissorRects( 1, &r );

				// Bind texture, Draw
				ID3D11ShaderResourceView* texture_srv = ( ID3D11ShaderResourceView* )pcmd->TextureId;
				ctx->PSSetShaderResources( 0, 1, &texture_srv );
				ctx->DrawIndexed( pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset );
			}
		}
		global_idx_offset += cmd_list->IdxBuffer.Size;
		global_vtx_offset += cmd_list->VtxBuffer.Size;
	}

	// Restore modified DX state
	ctx->RSSetScissorRects( old.ScissorRectsCount, old.ScissorRects );
	ctx->RSSetViewports( old.ViewportsCount, old.Viewports );
	ctx->RSSetState( old.RS ); if( old.RS ) old.RS->Release();
	ctx->OMSetBlendState( old.BlendState, old.BlendFactor, old.SampleMask ); if( old.BlendState ) old.BlendState->Release();
	ctx->OMSetDepthStencilState( old.DepthStencilState, old.StencilRef ); if( old.DepthStencilState ) old.DepthStencilState->Release();
	ctx->PSSetShaderResources( 0, 1, &old.PSShaderResource ); if( old.PSShaderResource ) old.PSShaderResource->Release();
	ctx->PSSetSamplers( 0, 1, &old.PSSampler ); if( old.PSSampler ) old.PSSampler->Release();
	ctx->PSSetShader( old.PS, old.PSInstances, old.PSInstancesCount ); if( old.PS ) old.PS->Release();
	for( UINT i = 0; i < old.PSInstancesCount; i++ ) if( old.PSInstances[ i ] ) old.PSInstances[ i ]->Release();
	ctx->VSSetShader( old.VS, old.VSInstances, old.VSInstancesCount ); if( old.VS ) old.VS->Release();
	ctx->VSSetConstantBuffers( 0, 1, &old.VSConstantBuffer ); if( old.VSConstantBuffer ) old.VSConstantBuffer->Release();
	ctx->GSSetShader( old.GS, old.GSInstances, old.GSInstancesCount ); if( old.GS ) old.GS->Release();
	for( UINT i = 0; i < old.VSInstancesCount; i++ ) if( old.VSInstances[ i ] ) old.VSInstances[ i ]->Release();
	ctx->IASetPrimitiveTopology( old.PrimitiveTopology );
	ctx->IASetIndexBuffer( old.IndexBuffer, old.IndexBufferFormat, old.IndexBufferOffset ); if( old.IndexBuffer ) old.IndexBuffer->Release();
	ctx->IASetVertexBuffers( 0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset ); if( old.VertexBuffer ) old.VertexBuffer->Release();
	ctx->IASetInputLayout( old.InputLayout ); if( old.InputLayout ) old.InputLayout->Release();
}

struct Shader
{
	Shader( std::uint8_t key, const std::uint8_t* const code, std::size_t size )
		: m_key( key )
		, m_code( code )
		, m_data( nullptr )
		, m_size( size )
	{
		m_data = new std::uint8_t[ m_size ];
	}

	~Shader()
	{
		memory::SafeDeleteArray( m_data );
	}

	bool empty() const
	{
		return ( m_size == 0 );
	}

	std::size_t size() const
	{
		return m_size;
	}

	const std::uint8_t* decrypt() const
	{
		for( std::size_t index = 0; index < m_size; index++ )
		{
			m_data[ index ] = m_code[ index ] ^ m_key;
		}

		return m_data;
	}

	std::uint8_t m_key = 0;
	const std::uint8_t* m_code = nullptr;
	std::uint8_t* m_data = nullptr;
	std::size_t m_size = 0;
};

bool ImGui_ImplDX11_CreateFontsTexture()
{
	auto& io = ImGui::GetIO();

	std::uint8_t* pixels = nullptr;
	
	std::int32_t width = 0;
	std::int32_t height = 0;

	io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );

	D3D11_TEXTURE2D_DESC texture2d_desc = { };

	texture2d_desc.Width = static_cast< UINT >( width );
	texture2d_desc.Height = static_cast< UINT >( height );
	texture2d_desc.MipLevels = 1;
	texture2d_desc.ArraySize = 1;
	texture2d_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texture2d_desc.SampleDesc.Count = 1;
	texture2d_desc.Usage = D3D11_USAGE_DEFAULT;
	texture2d_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texture2d_desc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA subresource_data = { };
	
	subresource_data.pSysMem = pixels;
	subresource_data.SysMemPitch = texture2d_desc.Width * 4;
	subresource_data.SysMemSlicePitch = 0;

	ID3D11Texture2D* texture2d = nullptr;

	auto result = g_pd3dDevice->CreateTexture2D( &texture2d_desc, &subresource_data, &texture2d );

	if( FAILED( result ) )
	{
		TRACE( "%s: ID3D11Device::CreateTexture2D( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
		return false;
	}
	
	D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc = { };

	shader_resource_view_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	shader_resource_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shader_resource_view_desc.Texture2D.MipLevels = texture2d_desc.MipLevels;
	shader_resource_view_desc.Texture2D.MostDetailedMip = 0;

	result = g_pd3dDevice->CreateShaderResourceView( texture2d, &shader_resource_view_desc, &g_pFontTextureView );

	memory::SafeRelease( texture2d );

	if( FAILED( result ) )
	{
		TRACE( "%s: ID3D11Device::CreateShaderResourceView( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
		return false;
	}

	// 
	// store characters texture
	// 
	io.Fonts->TexID = static_cast< ImTextureID >( g_pFontTextureView );

	D3D11_SAMPLER_DESC sampler_desc = { };

	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.MipLODBias = 0.f;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampler_desc.MinLOD = 0.f;
	sampler_desc.MaxLOD = 0.f;

	result = g_pd3dDevice->CreateSamplerState( &sampler_desc, &g_pFontSampler );

	if( FAILED( result ) )
	{
		TRACE( "%s: ID3D11Device::CreateSamplerState( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
		return false;
	}

	return true;
}

bool ImGui_ImplDX11_CreateDeviceObjects()
{
	if( !g_pd3dDevice )
	{
		TRACE( "%s: g_pd3dDevice is nullptr!", ATOM_FUNCTION );
		return false;
	}

	if( g_pFontSampler )
	{
		// 
		// release resources
		// 
		ImGui_ImplDX11_InvalidateDeviceObjects();
	}

	Shader vs( g_vs_key, g_vs_code, g_vs_size );

	auto result = g_pd3dDevice->CreateVertexShader( vs.decrypt(), vs.size(), nullptr, &g_pVertexShader );

	if( FAILED( result ) )
	{
		TRACE( "%s: ID3D11Device::CreateVertexShader( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
		return false;
	}

	auto position = SECURE_STRING( "POSITION" );
	auto texcoord = SECURE_STRING( "TEXCOORD" );
	auto color = SECURE_STRING( "COLOR" );

	D3D11_INPUT_ELEMENT_DESC input_element_desc[] =
	{
		{ position.decrypt(), 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0x0000, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ texcoord.decrypt(), 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0x0008, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ color.decrypt(), 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0x0010, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	result = g_pd3dDevice->CreateInputLayout( input_element_desc, ARRAYSIZE( input_element_desc ), vs.decrypt(), vs.size(), &g_pInputLayout );

	if( FAILED( result ) )
	{
		TRACE( "%s: ID3D11Device::CreateInputLayout( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
		return false;
	}

	D3D11_BUFFER_DESC buffer_desc =
	{
		sizeof( VERTEX_CONSTANT_BUFFER ),
		D3D11_USAGE_DYNAMIC,
		D3D11_BIND_CONSTANT_BUFFER,
		D3D11_CPU_ACCESS_WRITE,
		0, 0,
	};

	result = g_pd3dDevice->CreateBuffer( &buffer_desc, nullptr, &g_pVertexConstantBuffer );

	if( FAILED( result ) )
	{
		TRACE( "%s: ID3D11Device::CreateBuffer( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
		return false;
	}

	Shader ps( g_ps_key, g_ps_code, g_ps_size );

	result = g_pd3dDevice->CreatePixelShader( ps.decrypt(), ps.size(), nullptr, &g_pPixelShader );

	if( FAILED( result ) )
	{
		TRACE( "%s: ID3D11Device::CreatePixelShader( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
		return false;
	}

	D3D11_BLEND_DESC blend_desc = { };
	
	blend_desc.AlphaToCoverageEnable = FALSE;
	blend_desc.RenderTarget[ 0 ].BlendEnable = TRUE;
	blend_desc.RenderTarget[ 0 ].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blend_desc.RenderTarget[ 0 ].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blend_desc.RenderTarget[ 0 ].BlendOp = D3D11_BLEND_OP_ADD;
	blend_desc.RenderTarget[ 0 ].SrcBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blend_desc.RenderTarget[ 0 ].DestBlendAlpha = D3D11_BLEND_ZERO;
	blend_desc.RenderTarget[ 0 ].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blend_desc.RenderTarget[ 0 ].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	result = g_pd3dDevice->CreateBlendState( &blend_desc, &g_pBlendState );

	if( FAILED( result ) )
	{
		TRACE( "%s: ID3D11Device::CreateBlendState( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
		return false;
	}

	D3D11_RASTERIZER_DESC rasterizer_desc = { };

	rasterizer_desc.FillMode = D3D11_FILL_SOLID;
	rasterizer_desc.CullMode = D3D11_CULL_NONE;
	rasterizer_desc.ScissorEnable = TRUE;
	rasterizer_desc.DepthClipEnable = TRUE;

	result = g_pd3dDevice->CreateRasterizerState( &rasterizer_desc, &g_pRasterizerState );

	if( FAILED( result ) )
	{
		TRACE( "%s: ID3D11Device::CreateRasterizerState( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
		return false;
	}

	D3D11_DEPTH_STENCIL_DESC depth_stencil_desc = { };

	depth_stencil_desc.DepthEnable = false;
	depth_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth_stencil_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	depth_stencil_desc.StencilEnable = false;
	depth_stencil_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depth_stencil_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depth_stencil_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	depth_stencil_desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	depth_stencil_desc.BackFace = depth_stencil_desc.FrontFace;
	
	result = g_pd3dDevice->CreateDepthStencilState( &depth_stencil_desc, &g_pDepthStencilState );

	if( FAILED( result ) )
	{
		TRACE( "%s: ID3D11Device::CreateDepthStencilState( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
		return false;
	}

	return ImGui_ImplDX11_CreateFontsTexture();
}

void ImGui_ImplDX11_InvalidateDeviceObjects()
{
	auto& io = ImGui::GetIO();

	if( !g_pd3dDevice )
	{
		TRACE( "%s: g_pd3dDevice is nullptr!", ATOM_FUNCTION );
		return;
	}

	memory::SafeRelease( g_pFontSampler );
	memory::SafeRelease( g_pFontTextureView );
	memory::SafeRelease( g_pIB );
	memory::SafeRelease( g_pVB );
	memory::SafeRelease( g_pBlendState );
	memory::SafeRelease( g_pDepthStencilState );
	memory::SafeRelease( g_pRasterizerState );
	memory::SafeRelease( g_pPixelShader );
	memory::SafeRelease( g_pPixelShaderBlob );
	memory::SafeRelease( g_pVertexConstantBuffer );
	memory::SafeRelease( g_pInputLayout );
	memory::SafeRelease( g_pVertexShader );
	memory::SafeRelease( g_pVertexShaderBlob );

	io.Fonts->TexID = nullptr;
}

bool ImGui_ImplDX11_Init( ID3D11Device* device, ID3D11DeviceContext* device_context )
{
	auto& io = ImGui::GetIO();

	// 
	// setup backend
	// 
	io.BackendRendererName = SECURE( "imgui_impl_dx11" );
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	
	// 
	// capture performance frequency & counter
	// 
	g_map_data.RtlQueryPerformanceFrequency( &g_dx11_frequency );
	g_map_data.RtlQueryPerformanceCounter( &g_dx11_counter );

	// 
	// capture factory
	// 
	
	IDXGIDevice* dxgi_device = nullptr;
	IDXGIAdapter* dxgi_adapter = nullptr;
	IDXGIFactory* dxgi_factory = nullptr;

	

	auto result = device->QueryInterface( IID_PPV_ARGS( &dxgi_device ) );

	if( SUCCEEDED( result ) )
	{
		result = dxgi_device->GetParent( IID_PPV_ARGS( &dxgi_adapter ) );

		if( SUCCEEDED( result ) )
		{
			result = dxgi_adapter->GetParent( IID_PPV_ARGS( &dxgi_factory ) );

			if( SUCCEEDED( result ) )
			{
				g_pd3dDevice = device;
				g_pd3dDeviceContext = device_context;
				g_pFactory = dxgi_factory;

				g_pd3dDevice->AddRef();
				g_pd3dDeviceContext->AddRef();
			}
			else
			{
				TRACE( "%s: IDXGIAdapter::GetParent( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
			}
		}
		else
		{
			TRACE( "%s: IDXGIDevice::GetParent( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
		}
	}
	else
	{
		TRACE( "%s: ID3D11Device::QueryInterface( ... ) error! (0x%08X)", ATOM_FUNCTION, result );
	}

	memory::SafeRelease( dxgi_adapter );
	memory::SafeRelease( dxgi_device );

	return ( g_pd3dDevice && g_pd3dDeviceContext && g_pFactory );
}

void ImGui_ImplDX11_Shutdown()
{
	ImGui_ImplDX11_InvalidateDeviceObjects();

	memory::SafeRelease( g_pFactory );
	memory::SafeRelease( g_pd3dDevice );
	memory::SafeRelease( g_pd3dDeviceContext );
}

void ImGui_ImplDX11_NewFrame()
{
	auto& io = ImGui::GetIO();

	UINT viewport_count = 1;
	D3D11_VIEWPORT viewport = { };

	// 
	// capture viewport
	// 
	g_pd3dDeviceContext->RSGetViewports( &viewport_count, &viewport );

	// 
	// update screen size
	// 
	io.DisplaySize = { viewport.Width, viewport.Height };

	// 
	// get current time
	// 
	win32::LARGE_INTEGER counter = { };
	g_map_data.RtlQueryPerformanceCounter( &counter );

	// 
	// update delta time
	// 
	io.DeltaTime = static_cast< float >( counter.QuadPart - g_dx11_counter.QuadPart ) / static_cast< float >( g_dx11_frequency.QuadPart );

	// 
	// store current counter
	// 
	g_dx11_counter = counter;

	if( !g_pFontSampler )
	{
		if( !ImGui_ImplDX11_CreateDeviceObjects() )
		{
			TRACE( "%s: ImGui_ImplDX11_CreateDeviceObjects() error!", ATOM_FUNCTION );
		}
	}
}