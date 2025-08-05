struct VS_Input
{
    float4 pos : POSITION;
    float4 color : COLOR0;
};

struct VS_Output
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
};

VS_Output main(VS_Input input)
{
    VS_Output output;
    output.pos = input.pos;
    output.color = input.color;
    return output;
}
