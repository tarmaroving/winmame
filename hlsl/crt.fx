//-----------------------------------------------------------------------------
// Effect File Variables
//-----------------------------------------------------------------------------

texture Diffuse;

sampler DiffuseSampler = sampler_state
{
	Texture   = <Diffuse>;
	MipFilter = LINEAR;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	AddressU = CLAMP;
	AddressV = CLAMP;
	AddressW = CLAMP;
};

//-----------------------------------------------------------------------------
// Vertex Definitions
//-----------------------------------------------------------------------------

struct VS_OUTPUT
{
	float4 Position : POSITION;
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
};

struct VS_INPUT
{
	float3 Position : POSITION;
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
	float2 Unused : TEXCOORD1;
};

struct PS_INPUT
{
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
	float4 Position : VPOS;
};

//-----------------------------------------------------------------------------
// Simple Vertex Shader
//-----------------------------------------------------------------------------

VS_OUTPUT vs_main(VS_INPUT Input)
{
	VS_OUTPUT Output = (VS_OUTPUT)0;
	
	Output.Position = float4(Input.Position.xyz, 1.0f);
	Output.Color = Input.Color;
	Output.TexCoord = Input.TexCoord;
	
	return Output;
}

//-----------------------------------------------------------------------------
// Simple Pixel Shader
//-----------------------------------------------------------------------------

float scurve(float x)
{
    return (3.0-2.0*x)*x*x;
}

float tooth(float x)
{
    x=frac(x);
    x=abs(x-0.5)*2.0;
    x=scurve(x);
    return x;
}

/*
vec4 crt(vec2 pos)
{
   float l=0.5+tooth(pos.y/3.0+0.5)*0.65;
   float dx=tooth(pos.y/3.0*1.6+1.0/4.0);
   vec3 rgb=vec3(
    tooth(pos.x/3.0*1.6+dx        ),
    tooth(pos.x/3.0*1.6+dx+1.0/3.0),
    tooth(pos.x/3.0*1.6+dx+2.0/3.0)
    )*0.7+0.7;
   return vec4(rgb*l, 1.0);
}
*/

float4 crt(float2 pos)
{
   float unit = 1.33;
   float l=0.5+tooth(pos.y/3.0+0.5)*1.0;
   float dx=tooth(pos.y/3.0*unit+1.0/4.0);
   float3 rgb=float3(
    tooth(pos.x/3.0*unit+dx        ),
    tooth(pos.x/3.0*unit+dx+1.0/3.0),
    tooth(pos.x/3.0*unit+dx+2.0/3.0)
    )*0.7+0.7;
   return float4(rgb*l, 1.0);
}

float4 crtv(float2 pos)
{
   float l=0.5+tooth(pos.x/3.0+0.5)*1.0;
   float dx=tooth(pos.x/3.0*1.6+1.0/4.0);
   float3 rgb=float3(
    tooth(pos.y/3.0*1.6+dx        ),
    tooth(pos.y/3.0*1.6+dx+1.0/3.0),
    tooth(pos.y/3.0*1.6+dx+2.0/3.0)
    )*0.7+0.7;
   return float4(rgb*l, 1.0);
}

uniform int GameWidth       = 320;
uniform int GameHeight      = 240;
uniform int GameOrientation = 0;

float4 ps_main(PS_INPUT Input) : COLOR
{
	float4 BaseTexel = tex2D(DiffuseSampler, Input.TexCoord);
	float4 Output = BaseTexel;
	
	float Gamma = 0.7f;
	BaseTexel.r = pow(Output.r, 1.0 / Gamma);
	BaseTexel.g = pow(Output.g, 1.0 / Gamma);
	BaseTexel.b = pow(Output.b, 1.0 / Gamma);
	Output = BaseTexel;
	
	float2 pos = Input.Position.xy;
	
	if (GameOrientation == 0) {
		pos.x *= 0.60;
		pos.y *= 1.00;
		Output = Output * crt(pos);
	} else {
		pos.x *= 0.80;
		pos.y *= 0.60;
		Output = Output * crtv(pos);
	}
	
	return Output;
}

//-----------------------------------------------------------------------------
// Simple Effect
//-----------------------------------------------------------------------------

technique DefaultTechnique
{
	pass Pass0
	{
		Lighting = FALSE;
		VertexShader = compile vs_3_0 vs_main();
		PixelShader  = compile ps_3_0 ps_main();
	}
}
