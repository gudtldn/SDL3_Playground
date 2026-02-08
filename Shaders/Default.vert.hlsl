struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
};

cbuffer MVPBuffer : register(b0, space1)
{
    float4x4 mvp;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(mvp, float4(input.position, 1.0));
    output.normal = input.normal;
    return output;
}
