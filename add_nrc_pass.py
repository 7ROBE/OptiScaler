import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/shaders/fsrd_preprocess/FSRDPreprocessor_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

# 1. Add include for the NRC query shader bytecode
old_inc = '#include "precompile/FSRDOutputComp_Shader.h"'
new_inc = '''#include "precompile/FSRDOutputComp_Shader.h" 
#include "precompile/FSRDNrcQuery_Shader.h"'''
assert old_inc in c
c = c.replace(old_inc, new_inc)

# 2. Add pipeline member to Impl (after m_compShader declaration)
old_member = """    ComputeState m_floorSeedShader;
    ComputeState m_floorFilterShader;"""
if "m_nrcQueryShader" not in c:
    # find the comp shader member
    import re
    m = re.search(r"(ComputeState m_compShader;)", c)
    assert m, "comp shader member not found"
    c = c.replace(m.group(1), m.group(1) + "\n    ComputeState m_nrcQueryShader;")

io.open(p, "w", encoding="utf-8").write(c)
print("include + member added")
