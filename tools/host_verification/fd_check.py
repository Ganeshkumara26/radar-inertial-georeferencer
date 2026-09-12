import random

def doppler_fn(pn, pe, pd, vn, ve, vd):
    r = (pn**2+pe**2+pd**2)**0.5
    return (vn*pn + ve*pe + vd*pd) / r

def code_jacobian_row(pn, pe, pd, vn, ve, vd):
    r2 = pn*pn+pe*pe+pd*pd
    r = r2**0.5
    h18 = (vn*pn*pn + vn*pe*pe + pe*ve*pd - pn*(vn*pn+ve*pe+vd*pd)) / (r2*r)
    h19 = (ve*pe*ve + ve*pn*pn + pn*vn*pd - pe*(vn*pn+ve*pe+vd*pd)) / (r2*r)
    h20 = (vd*pd*vd + vd*pn*pn + pn*vn*pe - pd*(vn*pn+ve*pe+vd*pd)) / (r2*r)
    h21, h22, h23 = pn/r, pe/r, pd/r
    return [h18,h19,h20,h21,h22,h23]

def fd_jacobian_row(pn, pe, pd, vn, ve, vd, eps=1e-4):
    base = [pn,pe,pd,vn,ve,vd]
    grads = []
    for i in range(6):
        p = base[:]; p[i]+=eps
        m = base[:]; m[i]-=eps
        grads.append((doppler_fn(*p)-doppler_fn(*m))/(2*eps))
    return grads

random.seed(42)
max_err = 0
for _ in range(5):
    pn,pe,pd = random.uniform(1,20), random.uniform(1,20), random.uniform(1,20)
    vn,ve,vd = random.uniform(-5,5), random.uniform(-5,5), random.uniform(-5,5)
    code = code_jacobian_row(pn,pe,pd,vn,ve,vd)
    fd = fd_jacobian_row(pn,pe,pd,vn,ve,vd)
    err = [abs(c-f) for c,f in zip(code,fd)]
    max_err = max(max_err, max(err))
    print(f"pn={pn:.2f} pe={pe:.2f} pd={pd:.2f} vn={vn:.2f} ve={ve:.2f} vd={vd:.2f}")
    print("  code:", [f"{x:.5f}" for x in code])
    print("  FD  :", [f"{x:.5f}" for x in fd])
    print("  err :", [f"{x:.5f}" for x in err])
print("\nMax abs error across samples:", max_err)
