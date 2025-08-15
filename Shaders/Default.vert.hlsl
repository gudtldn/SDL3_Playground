cbuffer ConstantBuf : register(b0)
{
    float4x4 MVP;
}

struct VertexInput
{
    float4 position : POSITION;
    float4 color : COLOR;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;

    output.position = mul(MVP, input.position);
    output.color = input.color;

    return output;
}
