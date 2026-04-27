# Improved-CE5-Project-Templates
These are improved versions of CryEngine 5's default project templates along with added Schematyc variations of existing ones

## How to install
1. Download the files from this repository, via either download zip or git clone
2. Copy the **Templates** folder and then paste it into this directory **%localappdata%\Programs\CRYENGINE Launcher\Engines\crytek\cryengine-57-lts\5.7.1**
3. Click replace all files when prompted

## Change notes
- Fixed/Improved movement code (old code waited for a networked input variable to be set which caused some bugs and unresponsiveness sometimes)
- Added jump ability
- Added spawnpoint helper model (same one used in GameSDK)
- Added Schematyc variations of the projects that don't have one
- Added navagent definitions to all projects (taken from isometric pathfinding template)
- Rigidbodies in example level are now set to network synced
- Fixed player rotation not syncing with sandbox viewport when entering play mode
- Fixed player rotation not syncing properly in multiplayer
- Minor code cleanup for some projects
- Updated playermodel mannequin data for some projects which had it outdated
