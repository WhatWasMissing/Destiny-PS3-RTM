#include "game/movement_addr.h"

namespace brps3::movement {

 
 
const Fingerprints kPadFingerprints = {
    {addr::PAD_POLL_PORT, "7C0802A648CC94D9F821FEB1F8010160"},
    {addr::PAD_POLL_PORTS, "7C0802A648CC8DF9F821FEE1F8010130"},
    {addr::INPUT_UPDATE, "7C0802A648CC9E5DF821FF51F80100C0"},
};

const Fingerprints kPhysicsFingerprints = {
    {addr::PHYSICS_COMPONENT, "7C0802A648BDF775F821FF71F80100A0"},
    {addr::SET_LINEAR_VELOCITY, "38A000D07C4020CE7C4519CE4E800020"},
    {addr::HKP_WORLD_GETTER, "3C60018980637CC84E800020"},
    {addr::WARP_TO_POSITION, "1040038C38A0009038C0003038E00080"},
    {addr::ISLAND_DTOR, "F821FF717C0802A6F80100A0FBE10088"},
};

const Fingerprints kViewFingerprints = {
    {addr::VIEW_SETUP, "7C0802A63181FFE84877F0AD4877F10D"},
    {addr::VIEW_SET_SIZE, "7C0802A648783331F821FF71F80100A0"},
    {addr::VIEW_SET_CAMERA, "7C0802A648783605F821FF81F8010090"},
};

Fingerprints allFingerprints() {
    Fingerprints all = kPadFingerprints;
    all.insert(all.end(), kPhysicsFingerprints.begin(), kPhysicsFingerprints.end());
    all.insert(all.end(), kViewFingerprints.begin(), kViewFingerprints.end());
    return all;
}

}   
