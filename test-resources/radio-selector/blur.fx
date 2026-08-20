texture sourceTexture;

float2 texelStep = float2(0.0, 0.0);
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

float4 BlurPixel(float2 textureCoordinate : TEXCOORD0) : COLOR0
{
    // Paired bilinear samples approximate a nine-tap Gaussian. The resource
    // reuses this compact kernel in several small separable passes so a broad
    // blur does not expose individual samples as horizontal/vertical ghosts.
    float4 color = tex2D(SourceSampler, textureCoordinate) * 0.2270270270;
    color += tex2D(SourceSampler, textureCoordinate + texelStep * 1.3846153846) * 0.3162162162;
    color += tex2D(SourceSampler, textureCoordinate - texelStep * 1.3846153846) * 0.3162162162;
    color += tex2D(SourceSampler, textureCoordinate + texelStep * 3.2307692308) * 0.0702702703;
    color += tex2D(SourceSampler, textureCoordinate - texelStep * 3.2307692308) * 0.0702702703;

    float luminance = dot(color.rgb, float3(0.299, 0.587, 0.114));
    color.rgb = lerp(color.rgb, luminance.xxx, desaturation) * brightness;
    return color;
}

technique RadioBlur
{
    pass P0
    {
        PixelShader = compile ps_2_0 BlurPixel();
    }
}

technique Fallback
{
    pass P0
    {
    }
}
