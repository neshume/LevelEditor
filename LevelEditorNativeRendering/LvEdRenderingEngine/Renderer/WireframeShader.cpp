//Copyright � 2014 Sony Computer Entertainment America LLC. See License.txt.

#include "../Core/NonCopyable.h"
#include "WireFrameShader.h"
#include <D3D11.h>
#include "Renderable.h"
#include "RenderBuffer.h"
#include "RenderUtil.h"
#include "../Core/Utils.h"
#include "RenderContext.h"
#include "RenderState.h"
#include "Model.h"

using namespace LvEdEngine;



void WireFrameShader::Begin(RenderContext* context)
{
    if(m_rcntx) return;		
	m_rcntx = context;
    
    ID3D11DeviceContext* d3dContext = context->Context();

	CbPerFrame cb;    
    cb.viewport = context->ViewPort();
    Matrix::Transpose(m_rcntx->Cam().View(),cb.viewXform);
    Matrix::Transpose(m_rcntx->Cam().Proj(),cb.projXform);	
	
    UpdateConstantBuffer(d3dContext,m_cbPerFrame,&cb,sizeof(cb));
	
	d3dContext->VSSetConstantBuffers(0,1,&m_cbPerFrame);    
    d3dContext->VSSetConstantBuffers(1,1,&m_cbPerObject);
    d3dContext->GSSetConstantBuffers(0,1,&m_cbPerFrame);            
    d3dContext->PSSetConstantBuffers(1,1,&m_cbPerObject);	

    d3dContext->VSSetShader(m_vsShader,NULL,0);
    d3dContext->GSSetShader(m_gsShader,NULL,0);
    d3dContext->PSSetShader(m_psShader,NULL,0);
    d3dContext->IASetInputLayout( m_layoutP);
    d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    d3dContext->RSSetState(m_rsFillCullBack);
    d3dContext->OMSetDepthStencilState(m_dpLessEqual,0);

    float bf[] = {0,0,0,0};
    d3dContext->OMSetBlendState(m_bsBlending,bf,0xFFFFFFFF);
}

void WireFrameShader::End()
{
    m_rcntx->Context()->GSSetShader(NULL,NULL,0);
    m_rcntx = NULL;
}

void WireFrameShader::SetCullMode(CullModeEnum cullMode)
{
    ID3D11DeviceContext* d3dContext = m_rcntx->Context();

    if(cullMode == CullMode::NONE)
        d3dContext->RSSetState(m_rsFillCullNone);
    else if(cullMode == CullMode::BACK)
        d3dContext->RSSetState(m_rsFillCullBack);
    else if(cullMode == CullMode::FRONT)
        d3dContext->RSSetState(m_rsFillCullFront);
}


void WireFrameShader::DrawNodes(const RenderNodeList& renderNodes)
{    
    CbPerObject cb;
    ID3D11DeviceContext* d3dContext = m_rcntx->Context();
    for ( auto it = renderNodes.begin(); it != renderNodes.end(); ++it )
    {
        const RenderableNode& r = (*it);
        Matrix::Transpose(r.WorldXform,cb.worldXform);   
        cb.color = r.diffuse;
        UpdateConstantBuffer(d3dContext,m_cbPerObject,&cb,sizeof(cb));
        uint32_t stride = r.mesh->vertexBuffer->GetStride();
        uint32_t offset = 0;
        uint32_t startIndex  = 0;
        uint32_t indexCount  = r.mesh->indexBuffer->GetCount();;
        uint32_t startVertex = 0;
        ID3D11Buffer* d3dvb  = r.mesh->vertexBuffer->GetBuffer();
        ID3D11Buffer* d3dib  = r.mesh->indexBuffer->GetBuffer();

        d3dContext->IASetVertexBuffers( 0, 1, &d3dvb, &stride, &offset );
        d3dContext->IASetIndexBuffer(d3dib,DXGI_FORMAT_R32_UINT,0);    
        d3dContext->DrawIndexed(indexCount,startIndex,startVertex);
    }
}

WireFrameShader::WireFrameShader(ID3D11Device* device)
  : Shader( Shaders::WireFrameShader)
{    
    m_rcntx = NULL;
    m_gsShader = NULL;
    // create cbuffers.
    m_cbPerFrame  = CreateConstantBuffer(device, sizeof(CbPerFrame));
    m_cbPerObject = CreateConstantBuffer(device, sizeof(CbPerObject));
        
    ID3DBlob* vsBlob = CompileShaderFromResource(L"WireFrameShader.hlsl", "VSSolidWire","vs_4_0", NULL);    
    ID3DBlob* gsBlob = CompileShaderFromResource(L"WireFrameShader.hlsl", "GSSolidWire","gs_4_0", NULL);    
    ID3DBlob* psBlob = CompileShaderFromResource(L"WireFrameShader.hlsl", "PSSolidWire","ps_4_0", NULL);

    assert(vsBlob);
    assert(gsBlob);
    assert(psBlob);
    
    m_vsShader = CreateVertexShader(device, vsBlob);
    m_gsShader = CreateGeometryShader(device, gsBlob);
    m_psShader = CreatePixelShader(device, psBlob);
    
    assert(m_vsShader);
    assert(m_gsShader);
    assert(m_psShader);
    
    // Define the input layout
    D3D11_INPUT_ELEMENT_DESC layoutP[] =
    {
       { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },        
    };
   
    // create input layout
    m_layoutP = CreateInputLayout(device, vsBlob, layoutP, ARRAYSIZE( layoutP ));
    assert(m_layoutP);

    // release the blobs
    vsBlob->Release();
    gsBlob->Release();
    psBlob->Release();

    // create state blocks
    RenderStateCache* rsCache = RenderContext::Inst()->GetRenderStateCache();
    D3D11_RASTERIZER_DESC rsDcr = rsCache->GetDefaultRsDcr();
    rsDcr.CullMode =  D3D11_CULL_NONE;
    rsDcr.MultisampleEnable = true;
    rsDcr.DepthBias = -10;      // tweak the depth bias so that wireframe rendering wont z fight with 'normal' rendering.
    device->CreateRasterizerState(&rsDcr,  &m_rsFillCullNone);

    rsDcr.CullMode = D3D11_CULL_BACK;
    device->CreateRasterizerState(&rsDcr,  &m_rsFillCullBack);

    rsDcr.CullMode = D3D11_CULL_FRONT;
    device->CreateRasterizerState(&rsDcr,  &m_rsFillCullFront);
    
    D3D11_DEPTH_STENCIL_DESC dsDcr = rsCache->GetDefaultDpDcr();
    dsDcr.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDcr.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    device->CreateDepthStencilState(&dsDcr,&m_dpLessEqual);

    D3D11_BLEND_DESC blendDcr = rsCache->GetDefaultBsDcr();
    D3D11_RENDER_TARGET_BLEND_DESC rtblendDcr = blendDcr.RenderTarget[0];

    rtblendDcr.BlendEnable = TRUE;
    rtblendDcr.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rtblendDcr.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rtblendDcr.BlendOp = D3D11_BLEND_OP_ADD;
    //rtblendDcr.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    //rtblendDcr.DestBlendAlpha = D3D11_BLEND_DEST_ALPHA;
    rtblendDcr.SrcBlendAlpha = D3D11_BLEND_ONE;
    rtblendDcr.DestBlendAlpha = D3D11_BLEND_ZERO;
    rtblendDcr.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rtblendDcr.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; 

    blendDcr.RenderTarget[0] = rtblendDcr;
    device->CreateBlendState(&blendDcr,&m_bsBlending);
    
}

// --------------------------------------------------------------------------------------------------
WireFrameShader::~WireFrameShader()
{

    SAFE_RELEASE(m_cbPerFrame);
    SAFE_RELEASE(m_cbPerObject);    
    SAFE_RELEASE(m_layoutP);
    SAFE_RELEASE(m_vsShader);
    SAFE_RELEASE(m_psShader);
    SAFE_RELEASE(m_gsShader);
    
    SAFE_RELEASE(m_rsFillCullNone);
    SAFE_RELEASE(m_rsFillCullBack);
    SAFE_RELEASE(m_rsFillCullFront);
    SAFE_RELEASE(m_dpLessEqual);
    SAFE_RELEASE(m_bsBlending);
}

void WireFrameShader::SetRenderFlag(RenderFlagsEnum rf)
{   
    if( rf & RenderFlags::RenderBackFace)
    {
        SetCullMode(CullMode::NONE);
    }
    else
    {
        SetCullMode(CullMode::BACK);
    }
}
