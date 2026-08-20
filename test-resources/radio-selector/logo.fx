texture sourceTexture;

float desaturation = 0.0;
float brightness = 1.0;

sampler SourceSampler = sampler_state
{
    Texture = <sourceTexture>;
    MinFilter = Linear;
    MagFilter = Linear;
    MipFilter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

float4 GradeLogo(float2 textureCoordinate : TEXCOORD0, float4 diffuse : COLOR0) : COLOR0
{
    float4 color = tex2D(SourceSampler, textureCoordinate);
    float luminance = dot(color.rgb, float3(0.299, 0.587, 0.114));
    color.rgb = lerp(color.rgb, luminance.xxx, desaturation) * brightness;
    return color * diffuse;
}

technique RadioLogoGrade
{
    pass P0
    {
        PixelShader = compile ps_2_0 GradeLogo();
    }
}

technique Fallback
{
    pass P0
    {
    }
}
