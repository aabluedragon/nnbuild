// Shared Duke3D/Redneck Rampage range input handlers.
// The including menus.cpp supplies Menu* types and Menu_EntryRange* hooks.

static void Menu_RunInput_EntryRangeInt32_MovementVerify(MenuEntry_t *entry, MenuRangeInt32_t *object, int32_t newValue)
{
    if (!Menu_EntryRangeInt32Modify(entry, newValue))
        *object->variable = newValue;
}

static void Menu_RunInput_EntryRangeInt32_MovementArbitrary(MenuEntry_t *entry, MenuRangeInt32_t *object, int32_t newValue)
{
    if (object->flags & EnforceIntervals)
    {
        int32_t const range = object->max - object->min;
        int32_t const maxInterval = object->steps - 1;
        int32_t const newValueIndex = roundscale(newValue - object->min, maxInterval, range);
        newValue = newValueIndex * range / maxInterval + object->min;
    }

    Menu_RunInput_EntryRangeInt32_MovementVerify(entry, object, newValue);
}

static void Menu_RunInput_EntryRangeInt32_Movement(MenuEntry_t *entry, MenuRangeInt32_t *object, MenuMovement_t direction)
{
    int32_t const oldValue = *object->variable;
    int32_t const range = object->max - object->min;
    int32_t const maxInterval = object->steps - 1;
    int32_t const oldValueIndex = roundscale(oldValue - object->min, maxInterval, range);
    int32_t const oldValueQuantized = oldValueIndex * range / maxInterval + object->min;
    int32_t newValueIndex = oldValueIndex;

    switch (direction)
    {
        case MM_Left:
            if (oldValueQuantized >= oldValue)
                --newValueIndex;
            if (newValueIndex >= 0)
                break;
            fallthrough__;
        case MM_AllTheWayLeft:
            newValueIndex = 0;
            break;

        case MM_Right:
            if (oldValueQuantized <= oldValue)
                ++newValueIndex;
            if (newValueIndex <= maxInterval)
                break;
            fallthrough__;
        case MM_AllTheWayRight:
            newValueIndex = maxInterval;
            break;

        default:
            break;
    }

    int32_t const newValue = newValueIndex * range / maxInterval + object->min;
    Menu_RunInput_EntryRangeInt32_MovementVerify(entry, object, newValue);
}

static void Menu_RunInput_EntryRangeFloat_MovementVerify(MenuEntry_t *entry, MenuRangeFloat_t *object, float newValue)
{
    if (!Menu_EntryRangeFloatModify(entry, newValue))
    {
        *object->variable = newValue;
        Menu_EntryRangeFloatDidModify(entry);
    }
}

static void Menu_RunInput_EntryRangeFloat_MovementArbitrary(MenuEntry_t *entry, MenuRangeFloat_t *object, float newValue)
{
    if (object->flags & EnforceIntervals)
    {
        float const range = object->max - object->min;
        float const maxInterval = (float)(object->steps - 1);
        float const newValueIndex = rintf((newValue - object->min) * maxInterval / range);
        newValue = newValueIndex * range / maxInterval + object->min;
    }

    Menu_RunInput_EntryRangeFloat_MovementVerify(entry, object, newValue);
}

static void Menu_RunInput_EntryRangeFloat_Movement(MenuEntry_t *entry, MenuRangeFloat_t *object, MenuMovement_t direction)
{
    float const oldValue = *object->variable;
    float const range = object->max - object->min;
    float const maxInterval = (float)(object->steps - 1);
    float const oldValueIndexUnrounded = (oldValue - object->min) * maxInterval / range;
    float const oldValueIndex = rintf(oldValueIndexUnrounded);
    float const oldValueQuantized = oldValueIndex * range / maxInterval + object->min;
    float newValueIndex = oldValueIndex;

    switch (direction)
    {
        case MM_Left:
            if (oldValueQuantized >= oldValue)
                newValueIndex -= 1.f;
            if (newValueIndex >= 0.f)
                break;
            fallthrough__;
        case MM_AllTheWayLeft:
            newValueIndex = 0.f;
            break;

        case MM_Right:
            if (oldValueQuantized <= oldValue)
                newValueIndex += 1.f;
            if (newValueIndex <= maxInterval)
                break;
            fallthrough__;
        case MM_AllTheWayRight:
            newValueIndex = maxInterval;
            break;

        default:
            break;
    }

    float const newValue = newValueIndex * range / maxInterval + object->min;
    Menu_RunInput_EntryRangeFloat_MovementVerify(entry, object, newValue);
}

#ifdef MENU_ENABLE_RANGEDOUBLE
static void Menu_RunInput_EntryRangeDouble_MovementVerify(/*MenuEntry_t *entry, */MenuRangeDouble_t *object, double newValue)
{
    if (!Menu_EntryRangeDoubleModify(/*entry, newValue*/))
        *object->variable = newValue;
}

static void Menu_RunInput_EntryRangeDouble_MovementArbitrary(/*MenuEntry_t *entry, */MenuRangeDouble_t *object, double newValue)
{
    if (object->flags & EnforceIntervals)
    {
        double const range = object->max - object->min;
        double const maxInterval = object->steps - 1;
        double const newValueIndex = rint((newValue - object->min) * maxInterval / range);
        newValue = newValueIndex * range / maxInterval + object->min;
    }

    Menu_RunInput_EntryRangeDouble_MovementVerify(/*entry, */object, newValue);
}

static void Menu_RunInput_EntryRangeDouble_Movement(/*MenuEntry_t *entry, */MenuRangeDouble_t *object, MenuMovement_t direction)
{
    double const oldValue = *object->variable;
    double const range = object->max - object->min;
    double const maxInterval = object->steps - 1;
    double const oldValueIndex = rint((oldValue - object->min) * maxInterval / range);
    double const oldValueQuantized = oldValueIndex * range / maxInterval + object->min;
    double newValueIndex = oldValueIndex;

    switch (direction)
    {
        case MM_Left:
            if (oldValueQuantized >= oldValue)
                newValueIndex -= 1.;
            if (newValueIndex >= 0.)
                break;
            fallthrough__;
        case MM_AllTheWayLeft:
            newValueIndex = 0.;
            break;

        case MM_Right:
            if (oldValueQuantized <= oldValue)
                newValueIndex += 1.;
            if (newValueIndex <= maxInterval)
                break;
            fallthrough__;
        case MM_AllTheWayRight:
            newValueIndex = maxInterval;
            break;

        default:
            break;
    }

    double const newValue = newValueIndex * range / maxInterval + object->min;
    Menu_RunInput_EntryRangeDouble_MovementVerify(/*entry, */object, newValue);
}
#endif
