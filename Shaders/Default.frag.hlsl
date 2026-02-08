struct PixelInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
};

cbuffer ColorBuffer : register(b0, space3)
{
    float4 u_color;
};

float4 main(PixelInput input) : SV_Target0
{
    // u_color.a가 0보다 크면 해당 색상을 사용 (기즈모/AABB용)
    if (u_color.a > 0.0)
    {
        return u_color;
    }
    
    // 그렇지 않으면 노멀 기반 색상 사용 (메쉬용)
    return float4(input.normal * 0.5 + 0.5, 1.0);
}
