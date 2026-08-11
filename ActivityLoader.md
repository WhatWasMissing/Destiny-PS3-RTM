# Activity Loader Guide

The Activity Loader module in **Destiny RTM** allows you to force-load specific missions, strikes, raids, and destinations by writing activity indices directly across 4 target memory addresses.

> [!WARNING]
> **Known Issue & Prerequisite:**  
> The **"Read Current"** command does not work as intended in the current build. Before sending a load command from the tool, **you must manually select or start an activity in-game first**. Attempting to overwrite the activity address without an active in-game target queued will lead to unexpected behavior or game crashes.

---

## Usage Steps

1. **Establish Connection:** Connect the web tool to your PS3 using your console's IP address.
2. **Set In-Game Target:** In Destiny, select or initiate an activity using the Director menu so the game registers an active activity state.
3. **Locate Activity Loader:** Expand the **Activity Loader** panel in the RTM tool interface.
4. **Choose Destination:**
   - Use the **Search activity…** field to filter through the destination list.
   - Choose your desired activity from the dropdown menu.
5. **Execute Load:** Click **Load Activity** to push the changes.

---

## Interface Reference

| Control | Description |
| :--- | :--- |
| **Search activity…** | Real-time text filter to quickly locate specific activities in the dropdown. |
| **Activity Dropdown** | Master selection list containing pre-configured activity indices. |
| **Load Activity** | Writes the target index across 4 memory addresses simultaneously. |
| **Read Current** | *(Known Issue)* Attempts to read the active activity index. Currently unreliable—select an activity in-game manually instead. |
