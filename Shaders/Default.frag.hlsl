struct PixelInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
};

float4 main(PixelInput input) : SV_Target0
{
    return float4(input.normal * 0.5 + 0.5, 1.0);
}