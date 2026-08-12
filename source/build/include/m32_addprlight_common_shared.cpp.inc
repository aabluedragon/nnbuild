static void addprlight_common1(_prlight *mylightptr, int32_t i)
{
    mylightptr->sector = SECT(i);
    Bmemcpy(mylightptr, &sprite[i], sizeof(vec3_t));
    mylightptr->range = SHT(i);
    copy_prlight_colors(mylightptr, i);
    mylightptr->angle = SA(i);
    mylightptr->horiz = SH(i);
    mylightptr->minshade = sprite[i].xoffset;
    mylightptr->maxshade = sprite[i].yoffset;

    // overridden for spot lights
    mylightptr->radius = mylightptr->faderadius = mylightptr->tilenum = 0;

    if (CS(i) & 2)
    {
        if (CS(i) & 512)
            mylightptr->priority = PR_LIGHT_PRIO_LOW;
        else
            mylightptr->priority = PR_LIGHT_PRIO_HIGH;
    }
    else
        mylightptr->priority = PR_LIGHT_PRIO_MAX;

    mylightptr->publicflags.negative = !!(CS(i) & 128);

    spritelightid[i] = polymer_addlight(mylightptr);
    if (spritelightid[i] >= 0)
        spritelightptr[i] = &prlights[spritelightid[i]];
}
