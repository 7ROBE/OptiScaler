import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

old = """        m_impl->Initialize(GetAsByteSpan(FSRDFloorSeed_cso), GetAsByteSpan(FSRDFloor_cso),
                           GetAsByteSpan(FSRDInputConv_cso), GetAsByteSpan(FSRDOutputComp_cso), isMode2);"""

new = """        m_impl->Initialize(GetAsByteSpan(FSRDFloorSeed_cso), GetAsByteSpan(FSRDFloor_cso),
                           GetAsByteSpan(FSRDInputConv_cso), GetAsByteSpan(FSRDOutputComp_cso), isMode2,
                           GetAsByteSpan(FSRDNrcQuery_cso));"""

assert old in c, "init call not found"
c = c.replace(old, new, 1)

# Need the include for FSRDNrcQuery_Shader.h in this cpp too (bytecode symbol)
inc_old = '#include "precompile/FSRDOutputComp_Shader.h"'
if inc_old in c and "FSRDNrcQuery_Shader.h" not in c:
    c = c.replace(inc_old, inc_old + '\n#include "precompile/FSRDNrcQuery_Shader.h"', 1)

io.open(p, "w", encoding="utf-8").write(c)
print("constructor init passes NRC bytecode")
