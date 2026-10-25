// Engine particle drawing, shared with the TriAPI regression tests.
const int ramp1[8] = {0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61};
const int ramp2[8] = {0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66};
const int ramp3[8] = {0x6d, 0x6b, 6, 5, 4, 3};

#define SPARK_COLORCOUNT 9
const int gSparkRamp[SPARK_COLORCOUNT] = {0xfe, 0xfd, 0xfc, 0x6f, 0x6e, 0x6d, 0x6c, 0x67, 0x60};

const color24 gTracerColors[] =
    {
        {255, 255, 255}, // White
        {255, 0, 0},     // Red
        {0, 255, 0},     // Green
        {0, 0, 255},     // Blue
        {0, 0, 0},       // Tracer default, filled in from cvars, etc.
        {255, 167, 17},  // Yellow-orange sparks
        {255, 130, 90},  // Yellowish streaks (garg)
        {55, 60, 144},   // Blue egon streak
        {255, 130, 90},  // More Yellowish streaks (garg)
        {255, 140, 90},  // More Yellowish streaks (garg)
        {200, 130, 90},  // More red streaks (garg)
        {255, 120, 70},  // Darker red streaks (garg)
};

void R_DrawParticles(void)
{
    if (CL_IsDevOverviewMode())
        return;

    if (!(r_draw_classify & DRAW_CLASSIFY_PARTICLES))
        return;

    gPrivateFuncs.R_FreeDeadParticles(&(*active_particles));

    GL_BeginDebugGroup("R_DrawParticles");

    vec3_t up, right;
    float  scale;

    GL_Bind((*particletexture));

    gEngfuncs.pTriAPI->RenderMode(kRenderTransTexture);
    // Native particles use glBegin directly. TriAPI Begin resets the surface
    // texture and can unbind the particle texture before submission.
    triapi_glBegin(GL_TRIANGLES);

    VectorScale(vup, 1.5, up);
    VectorScale(vright, 1.5, right);

    float frametime = (*cl_time) - (*cl_oldtime);
    float time3     = frametime * 15;
    float time2     = frametime * 10; // 15;
    float time1     = frametime * 5;
    float grav      = frametime * (r_params.movevars ? r_params.movevars->gravity : 800) * 0.05;
    float dvel      = 4 * frametime;

    for (auto p = (*active_particles); p; p = p->next)
    {
        if (p->type != pt_blob)
        {
            // hack a scale up to keep particles from disappearing
            scale = (p->org[0] - r_origin[0]) * vpn[0] + (p->org[1] - r_origin[1]) * vpn[1] + (p->org[2] - r_origin[2]) * vpn[2];
            if (scale < 20)
                scale = 1;
            else
                scale = 1 + scale * 0.004;

            auto pb = &(*host_basepal)[(int)p->color * 4];

            byte rgba[4];
            rgba[0] = pb[2];
            rgba[1] = pb[1];
            rgba[2] = pb[0];
            rgba[3] = 255;

            gEngfuncs.pTriAPI->Color4ub(rgba[0], rgba[1], rgba[2], rgba[3]);
            gEngfuncs.pTriAPI->TexCoord2f(0, 0);
            gEngfuncs.pTriAPI->Vertex3fv(p->org);
            gEngfuncs.pTriAPI->TexCoord2f(1, 0);
            gEngfuncs.pTriAPI->Vertex3f(p->org[0] + up[0] * scale, p->org[1] + up[1] * scale, p->org[2] + up[2] * scale);
            gEngfuncs.pTriAPI->TexCoord2f(0, 1);
            gEngfuncs.pTriAPI->Vertex3f(p->org[0] + right[0] * scale, p->org[1] + right[1] * scale, p->org[2] + right[2] * scale);
        }

        if (p->type != pt_clientcustom)
        {
            p->org[0] += p->vel[0] * frametime;
            p->org[1] += p->vel[1] * frametime;
            p->org[2] += p->vel[2] * frametime;
        }

        switch (p->type)
        {
            case pt_static:
                break;

            case pt_fire:
                p->ramp += time1;
                if (p->ramp >= 6)
                    p->die = -1;
                else
                {
                    p->color       = ramp3[(int)p->ramp];
                    p->packedColor = 0;
                }
                p->vel[2] += grav;
                break;

            case pt_explode:
                p->ramp += time2;
                if (p->ramp >= 8)
                    p->die = -1;
                else
                {
                    p->color       = ramp1[(int)p->ramp];
                    p->packedColor = 0;
                }
                for (int i = 0; i < 3; i++)
                    p->vel[i] += p->vel[i] * dvel;
                p->vel[2] -= grav;
                break;

            case pt_explode2:
                p->ramp += time3;
                if (p->ramp >= 8)
                    p->die = -1;
                else
                {
                    p->color       = ramp2[(int)p->ramp];
                    p->packedColor = 0;
                }
                for (int i = 0; i < 3; i++)
                    p->vel[i] -= p->vel[i] * frametime;
                p->vel[2] -= grav;
                break;

            case pt_blob:
            case pt_blob2:
                p->ramp += time2;
                if (p->ramp >= SPARK_COLORCOUNT)
                    p->ramp = 0;

                p->color       = gSparkRamp[(int)p->ramp];
                p->packedColor = 0;

                for (int i = 0; i < 3; i++)
                    p->vel[i] += p->vel[i] * frametime * 0.5;
                p->vel[2] -= grav * 5;
                p->type = gEngfuncs.pfnRandomLong(0, 3) ? pt_blob : pt_blob2;
                break;

            case pt_grav:
                p->vel[2] -= grav * 20;
                break;

            case pt_slowgrav:
                p->vel[2] -= grav;
                break;

            case pt_vox_grav:
                p->vel[2] -= grav * 8;
                break;

            case pt_vox_slowgrav:
                p->vel[2] -= grav * 4;
                break;

            case pt_clientcustom:

                if (p->callback)
                    p->callback(p, frametime);

                break;
        }
    }

    gEngfuncs.pTriAPI->End();

    gPrivateFuncs.R_TracerDraw();
    gPrivateFuncs.R_BeamDrawList();

    gEngfuncs.pTriAPI->RenderMode(kRenderNormal);

    GL_EndDebugGroup();
}
