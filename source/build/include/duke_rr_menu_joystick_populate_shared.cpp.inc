// Shared Duke3D/Redneck Rampage joystick menu population.
// The including menus.cpp supplies joystick/menu globals and controller helpers.

static void Menu_PopulateJoystick(void)
{
    for (int i = 0; i < joystick.numButtons + 4*joystick.numHats + 2*joystick.isGameController; ++i)
    {
        if (i < joystick.numButtons)
        {
            if (i == CONTROLLER_BUTTON_START || !joyHasButton(i))
            {
                ME_JOYSTICKBTNS[i] ={};
                ME_JOYSTICKBTNS[i].flags = MEF_Hidden;
                continue;
            }

            auto const name = joyGetName(1, i);
            Bassert(name != nullptr);

            Bstrncpy(MenuJoystickNames[i], name, MAXJOYBUTTONSTRINGLENGTH);
        }
        else
        {
            if (joystick.numHats && i < joystick.numButtons + 4*joystick.numHats)
            {
                Bsnprintf(MenuJoystickNames[i], MAXJOYBUTTONSTRINGLENGTH, "Hat %d %s", ((i - joystick.numButtons) >> 2),
                          MenuJoystickHatDirections[((i - joystick.numButtons)) % 4]);
            }
            else if (joystick.isGameController)
            {
                auto const name = joyGetName(0, i - joystick.numButtons + 4);
                Bassert(name != nullptr);
                Bstrncpy(MenuJoystickNames[i], name, MAXJOYBUTTONSTRINGLENGTH);
            }
        }

        MEL_JOYSTICKBTNS[i] = &ME_JOYSTICKBTNS[i];
        ME_JOYSTICKBTNS[i] = ME_MOUSEJOYSETUPBTNS_TEMPLATE;
        ME_JOYSTICKBTNS[i].name = MenuJoystickNames[i];
        ME_JOYSTICKBTNS[i].entry = &MEO_JOYSTICKBTNS[i];
        MEO_JOYSTICKBTNS[i] = MEO_MOUSEJOYSETUPBTNS_TEMPLATE;

        if (i < joystick.numButtons + 4*joystick.numHats)
            MEO_JOYSTICKBTNS[i].data = &ud.config.JoystickFunctions[i][0];
        else if (joystick.isGameController)
            MEO_JOYSTICKBTNS[i].data = &ud.config.JoystickDigitalFunctions[i - joystick.numButtons - 4*joystick.numHats + 4][1];
    }

    M_JOYSTICKBTNS.numEntries = joystick.numButtons + 4*joystick.numHats + 2*joystick.isGameController;

    for (int i = 0; i < joystick.numAxes; ++i)
    {
        auto const name = joyGetName(0, i);
        Bassert(name != nullptr);
        ME_JOYSTICKAXES[i] = ME_JOYSTICKAXES_TEMPLATE;
        Bstrncpy(MenuJoystickAxes[i], name, MAXJOYBUTTONSTRINGLENGTH);
        ME_JOYSTICKAXES[i].name = MenuJoystickAxes[i];
        MEL_JOYSTICKAXES[i] = &ME_JOYSTICKAXES[i];
    }
    M_JOYSTICKAXES.numEntries = joystick.numAxes;
}
