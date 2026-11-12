// Texture axes define tangent directions independently of the BSP face side.
inline void R_BuildBrushFaceTangentBasis(const msurface_t* surf, CWorldSurfaceBrushFace& face)
{
    VectorCopy(surf->texinfo->vecs[0], face.s_tangent);
    VectorCopy(surf->texinfo->vecs[1], face.t_tangent);
    VectorNormalize(face.s_tangent);
    VectorNormalize(face.t_tangent);
    VectorCopy(surf->plane->normal, face.normal);

    if (surf->flags & SURF_PLANEBACK)
    {
        // The face normal changes side; UV gradients still follow texinfo.
        VectorInverse(face.normal);
    }
}

inline decalvertextbn_t R_BuildDecalTangentBasis(const CWorldSurfaceBrushFace& face, int decalFlags)
{
    decalvertextbn_t basis;
    VectorCopy(face.normal, basis.normal);
    VectorCopy(face.s_tangent, basis.s_tangent);
    VectorCopy(face.t_tangent, basis.t_tangent);
    // R_DecalVertsClip mirrors these UV axes without changing the face normal.
    if (decalFlags & FDECAL_HFLIP)
        VectorInverse(basis.s_tangent);
    if (decalFlags & FDECAL_VFLIP)
        VectorInverse(basis.t_tangent);
    return basis;
}
