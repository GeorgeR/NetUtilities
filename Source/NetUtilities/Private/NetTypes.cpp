#include "NetTypes.h"

void FSingle_Normalized::SetValue(const float InValue)
{
	Value = static_cast<uint8>(InValue * 255);
}

float FSingle_Normalized::GetValue() const
{
	return static_cast<float>(static_cast<float>(Value) / 255);
}

void FSingle_SignedNormalized::SetValue(const float InValue)
{
	Value = static_cast<int8>(InValue * 127);
}

float FSingle_SignedNormalized::GetValue() const
{
	return static_cast<float>(static_cast<float>(Value) / 127);
}

void FQuat_NetQuantize::SetValue(const FQuat InValue)
{
	Value.X = InValue.X;
	Value.Y = InValue.Y;
	Value.Z = InValue.Z;
	W = InValue.W;
}

FQuat FQuat_NetQuantize::GetValue() const
{
	FQuat Result(Value.X, Value.Y, Value.Z, W.GetValue());
	return Result;
}