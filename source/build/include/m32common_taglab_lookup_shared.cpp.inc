int32_t taglab_add(const char *label, int16_t tag)
{
    const char *otaglabel;
    char buf[TAGLAB_MAX];
    int32_t olabeltag, diddel=0;

    if (tag < 0)
        return -1;

    Bstrncpyz(buf, label, sizeof(buf));
    // upcase the tag for storage and comparison
    tstrtoupper(buf);

    otaglabel = g_taglab.label[tag];
    if (otaglabel)
    {
        if (!Bstrcasecmp(otaglabel, buf))
            return 0;

//        hash_delete(&g_taglab.hashtab, g_taglab.label[tag]);

        // a label having the same tag number as 'tag' is deleted
        DO_FREE_AND_NULL(g_taglab.label[tag]);
        diddel |= 1;
    }
    else
    {
        olabeltag = hash_findcase(&g_taglab.hashtab, buf);
        if (olabeltag==tag)
            return 0;

        if (olabeltag>=0)
        {
            // the label gets assigned to a new tag number ('tag deleted')
            DO_FREE_AND_NULL(g_taglab.label[olabeltag]);
            diddel |= 2;
        }
    }

    if (!diddel)
        g_taglab.numlabels++;
    g_taglab.label[tag] = Xstrdup(buf);
//initprintf("added %s %d to hash\n", g_taglab.label[tag], tag);
    hash_add(&g_taglab.hashtab, g_taglab.label[tag], tag, 1);

    return diddel;
}

const char *taglab_getlabel(int16_t tag)
{
    if (tag < 0)  // || tag>=32768 implicitly
        return NULL;

    return g_taglab.label[tag];
}

int32_t taglab_gettag(const char *label)
{
    char buf[TAGLAB_MAX];

    Bstrncpyz(buf, label, TAGLAB_MAX);

    // need to upcase since hash_findcase doesn't work as expected:
    // getting the code is still (necessarily) case-sensitive...
    tstrtoupper(buf);

    return hash_findcase(&g_taglab.hashtab, buf);
}
////////// end tag labeling system //////////
