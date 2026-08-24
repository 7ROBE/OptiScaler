import io

p = r"D:/Downloads/fsr rr/OptiScaler/OptiScaler/upscalers/fsr31/FSRDFeature_Dx12.cpp"
c = io.open(p, encoding="utf-8").read()

# Move the 5 static buffer decls to right after the includes (before EvaluateInternal).
decls = """static Microsoft::WRL::ComPtr<ID3D12Resource> nrcBufA;
static Microsoft::WRL::ComPtr<ID3D12Resource> nrcBufB;
static Microsoft::WRL::ComPtr<ID3D12Resource> nrcBufC;
static Microsoft::WRL::ComPtr<ID3D12Resource> nrcBufD;
static Microsoft::WRL::ComPtr<ID3D12Resource> nrcBufE;"""

# Remove from current location
assert decls in c
c = c.replace(decls + "\n", "", 1)

# Insert after MathUtils.h include
inc = '#include "MathUtils.h"'
assert inc in c
c = c.replace(inc, inc + "\n\n" + decls, 1)

io.open(p, "w", encoding="utf-8").write(c)
print("buffer statics moved to top")
