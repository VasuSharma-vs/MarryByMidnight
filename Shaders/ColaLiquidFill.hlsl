// Vertex-stage fill resolver. Manual previews use LiquidLevel; managed runtime
// instances opt into their own contents. A cached plane must match that source.
float3 upWS = normalize(InstanceUpWS);
float3 rightWS = normalize(InstanceRightWS - upWS * dot(InstanceRightWS, upWS));
float3 forwardWS = normalize(cross(upWS, rightWS));
float3 legacyTilt = float3(VelocityWS.xy * SloshStrength * 0.03, 0);
legacyTilt *= min(1.0, 0.35 / max(length(legacyTilt), 0.0001));
float3 normalWS = normalize(float3(legacyTilt.xy, 1));
if (NativeMotion > 0.5)
    normalWS = normalize(float3(NativeSurfaceNormalWS.xy, max(NativeSurfaceNormalWS.z, 0.6)));
float3 g = normalize(float3(dot(normalWS,rightWS), dot(normalWS,forwardWS), dot(normalWS,upWS)));
bool instanceMode = UseInstanceLiquidData > 0.5 && InstanceVolumeValid > 0.5 && NativeMotion < 0.5;
float fill = saturate(instanceMode ? InstanceFill : LiquidLevel);
float halfHeight = max(ContainerHeight * 0.5, 0.01);
float radius = max(ContainerRadius, 0.01);
float axial = abs(g.z) * halfHeight;
float radial = length(g.xy) * radius;
float extent = axial + radial;
float offset = 0;

// Runtime planes are computed once per data/pose update on CPU. Artist previews
// and invalidated native caches solve here, once per vertex rather than per pixel.
[branch] if (instanceMode) offset = InstancePlaneOffset;
else if (NativeMotion > 0.5 && abs(NativeCachedFill - fill) < 0.0001) offset = NativePlaneOffset;
else if (fill <= 0.0001) offset = -extent;
else if (fill >= 0.9999) offset = extent;
else if (abs(fill - 0.5) < 0.00001) offset = 0;
else
{
    struct CylinderFillCDF
    {
        float Disk(float u)
        {
            u = clamp(u,-1.0,1.0);
            return 0.5 + (asin(u) + u*sqrt(max(0.0,1.0-u*u))) / 3.14159265359;
        }
        float Integral(float t, float b)
        {
            if (t <= -b) return 0;
            if (t >= b) return t;
            float u = t/b;
            float r = sqrt(max(0.0,1.0-u*u));
            return b*(0.5*u + (u*asin(u)+r-r*r*r/3.0)/3.14159265359);
        }
        float Fraction(float h, float a, float b)
        {
            if (b < 0.00001) return saturate(0.5+h/(2.0*max(a,0.00001)));
            if (a < 0.00001) return Disk(h/b);
            return saturate((Integral(h+a,b)-Integral(h-a,b))/(2.0*a));
        }
    } solver;
    float lo = -extent, hi = extent;
    [unroll] for (int i=0;i<14;++i)
    {
        float mid = (lo+hi)*0.5;
        if (solver.Fraction(mid,axial,radial)<fill) lo=mid;
        else hi=mid;
    }
    offset = (lo+hi)*0.5;
}
return float2(fill,offset);
