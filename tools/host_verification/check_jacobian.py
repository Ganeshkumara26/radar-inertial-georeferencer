import sympy as sp

pn, pe, pd, vn, ve, vd = sp.symbols('pn pe pd vn ve vd', real=True)
r2 = pn**2 + pe**2 + pd**2
r = sp.sqrt(r2)
doppler = (vn*pn + ve*pe + vd*pd) / r

# Analytic partials
d_dpn = sp.diff(doppler, pn)
d_dpe = sp.diff(doppler, pe)
d_dpd = sp.diff(doppler, pd)
d_dvn = sp.diff(doppler, vn)
d_dve = sp.diff(doppler, ve)
d_dvd = sp.diff(doppler, vd)

print("Analytic d(doppler)/d(pn) =", sp.simplify(d_dpn))
print("Analytic d(doppler)/d(pe) =", sp.simplify(d_dpe))
print("Analytic d(doppler)/d(pd) =", sp.simplify(d_dpd))
print("Analytic d(doppler)/d(vn) =", sp.simplify(d_dvn))
print("Analytic d(doppler)/d(ve) =", sp.simplify(d_dve))
print("Analytic d(doppler)/d(vd) =", sp.simplify(d_dvd))

# Code's expressions (H[18..23])
code_dpn = (vn*pn*pn + vn*pe*pe + pe*ve*pd - pn*(vn*pn+ve*pe+vd*pd)) / (r2*r)
code_dpe = (ve*pe*ve + ve*pn*pn + pn*vn*pd - pe*(vn*pn+ve*pe+vd*pd)) / (r2*r)
code_dpd = (vd*pd*vd + vd*pn*pn + pn*vn*pe - pd*(vn*pn+ve*pe+vd*pd)) / (r2*r)

print()
print("Code d(doppler)/d(pn) - Analytic =", sp.simplify(code_dpn - d_dpn))
print("Code d(doppler)/d(pe) - Analytic =", sp.simplify(code_dpe - d_dpe))
print("Code d(doppler)/d(pd) - Analytic =", sp.simplify(code_dpd - d_dpd))
