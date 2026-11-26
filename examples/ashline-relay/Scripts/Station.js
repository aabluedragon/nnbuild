const USE = 1 << 29;
const CELL_PICKUP = 406;
const SEAL_RELEASE = 404;
const TRANSMIT = 405;
const COOLANT_DAMAGE = 407;
const OPEN_CEILING = -88 * 256;

const mission = {
    cell: false,
    gateOpen: false,
    transmitted: false,
    message: "",
    messageTime: 0,
    coolantTimer: 0,
};

function identify(component) {
    if (component.kind === "sector") {
        switch (nnb.getSector(component.index, "hitag")) {
            case 20201: return "seal";
            case 20202: return "coolant";
        }
    } else if (component.kind === "sprite") {
        switch (nnb.getActor(component.index, "picnum")) {
            case 3860: return "relay";
            case 3850: return "cell";
            case 3872: return "indicator";
            case 3876: return "uplink";
        }
    }
    return "unknown";
}

function signal(message, seconds) {
    mission.message = message;
    mission.messageTime = seconds;
}

function playerNear(sprite, distance) {
    if (nnb.getPlayer(0, "cursectnum") !== nnb.getActor(sprite, "sectnum")) return false;
    const dx = nnb.getActor(sprite, "x") - nnb.getPlayer(0, "posx");
    const dy = nnb.getActor(sprite, "y") - nnb.getPlayer(0, "posy");
    return dx * dx + dy * dy < distance * distance;
}

export function onAttach(component) {
    const role = identify(component);
    component.state.role = role;
    component.state.elapsed = 0;
    component.state.phase = 0;
    component.state.z = component.kind === "sprite" ? nnb.getActor(component.index, "z") : 0;
    if (role === "relay") nnb.log("Ashline Relay online");
}

function updateRelay(component, dt) {
    const frame = Math.floor(component.state.elapsed * (mission.cell ? 12 : 7));
    const frames = [3861, 3860, 3861, 3862, 3863, 3862];
    nnb.setActor(component.index, "picnum", frames[frame % frames.length]);
    nnb.setActor(component.index, "z", component.state.z + Math.round(720 * Math.sin(component.state.elapsed * 4)));
    mission.coolantTimer = Math.max(0, mission.coolantTimer - dt);
    mission.messageTime = Math.max(0, mission.messageTime - dt);
}

function updateCoolant(component) {
    const index = component.index;
    const phase = Math.floor(component.state.elapsed * 7) % 4;
    if (component.state.phase !== phase) {
        nnb.setSector(index, "floorpicnum", 3795 + phase);
        component.state.phase = phase;
    }
    if (nnb.getPlayer(0, "cursectnum") !== index || !nnb.getPlayer(0, "on_ground") || mission.coolantTimer > 0) return;
    const actor = nnb.player().sprite;
    const health = nnb.getActor(actor, "extra");
    if (health <= 0) return;
    nnb.setActor(actor, "extra", Math.max(0, health - 5));
    mission.coolantTimer = 0.55;
    nnb.sound(COOLANT_DAMAGE);
    signal("COOLANT BURN  /  RETURN TO THE WALKWAY", 1.6);
}

function updateSeal(component) {
    if (!mission.cell || mission.gateOpen) return;
    const index = component.index;
    const current = nnb.getSector(index, "ceilingz");
    const next = Math.max(OPEN_CEILING, current - 832);
    nnb.setSector(index, "ceilingz", next);
    if (next === OPEN_CEILING) {
        mission.gateOpen = true;
        nnb.log("Ashline Relay: uplink seal open");
    }
}

function updateCell(component) {
    if (mission.cell) return;
    if (playerNear(component.index, 600)) {
        mission.cell = true;
        nnb.setActor(component.index, "cstat", 32768);
        nnb.sound(CELL_PICKUP);
        nnb.sound(SEAL_RELEASE);
        signal("BLUE CELL ONLINE  /  UPLINK SEAL OPENING", 3.2);
        nnb.log("Ashline Relay: blue cell recovered");
    } else {
        nnb.setActor(component.index, "z", component.state.z + Math.round(620 * Math.sin(component.state.elapsed * 5)));
    }
}

function updateUplink(component) {
    if (mission.transmitted || !mission.gateOpen || !playerNear(component.index, 860)) return;
    signal("USE THE UPLINK TERMINAL TO TRANSMIT", 0.35);
    if (!(nnb.getInput(0, "bits") & USE)) return;
    mission.transmitted = true;
    nnb.setActor(component.index, "picnum", 3875);
    nnb.sound(TRANSMIT);
    signal("TRANSMISSION COMPLETE", 3);
    nnb.setPlayer(0, "customexitsound", -1);
    nnb.setUserdef("eog", 1);
    nnb.setPlayer(0, "timebeforeexit", 90);
    nnb.log("Ashline Relay: transmission complete");
}

export function onFixedUpdate(component, dt) {
    component.state.elapsed += dt;
    switch (component.state.role) {
        case "relay": updateRelay(component, dt); break;
        case "coolant": updateCoolant(component); break;
        case "seal": updateSeal(component); break;
        case "cell": updateCell(component); break;
        case "indicator":
            if (mission.gateOpen && component.state.phase === 0) {
                nnb.setActor(component.index, "picnum", 3878);
                component.state.phase = 1;
            }
            break;
        case "uplink": updateUplink(component); break;
    }
}

export function onUpdate(component) {
    if (component.state.role !== "relay") return;
    const health = Math.max(0, nnb.getActor(nnb.player().sprite, "extra"));
    const ammo = nnb.getPlayer(0, "ammo_amount", 1);
    const objective = mission.transmitted ? "RELAY LINK ESTABLISHED"
        : mission.cell ? "02  /  REACH THE UPLINK AND TRANSMIT"
        : "01  /  RECOVER THE BLUE CELL IN COOLANT";
    nnb.drawText(710, 9, 23, objective, 0, -12);
    nnb.drawText(711, 9, 187, "SUIT " + health + "    PULSE " + ammo, 0, -12);
    if (mission.messageTime > 0) nnb.drawText(712, 9, 35, mission.message, 0, -14);
    else nnb.clearText(712);
}

export function onDetach(component) {
    if (component.state.role !== "relay") return;
    for (const id of [710, 711, 712]) nnb.clearText(id);
}
