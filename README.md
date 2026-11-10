![Fighting the Episode 3 Blob](screenshots/image.png)

# READ THIS FIRST
Guess what: The episode 3 codebase leaked. 
[Join discord to help contribute](https://discord.gg/r5zAV6wCm)


## Features
* All episode 3 code
* Most things working (but not all, I only has 2 hours to make all this today!)
* All assets that have been found
* This code also works as a HL2 port to Alien Swarm 

## Running
If you use Source Filmmaker and already have it installed, skip to step 2.
1. Install Source Filmmaker, move the Source Filmmaker hl2 folder in that installation to somewhere on your computer and then you can uninstall it
2. Install Alien Swarm
3. Download this project to some place on your hard drive
4. Download and extract valve_asw_content.7z from (it has the hl2, ep1, and ep2 content ported to Alien Swarm, but no maps. You can officially find these assets on steam in the free app source filmmaker, but it is easier this way) this [link](https://drive.google.com/file/d/1GSNoyJJHDbT-nVyJYj2p91OIsnEnaxKn/view?usp=sharing)
5. Replace "E:\valve_asw_content\*" in (wherever you installed this project)/game/ep3/gameinfo.txt to wherever that hl2 folder is (if you have Source Filmmaker and have not moved the hl2 folder, change it to the path of the one you have installed)
6. If you have not installed Alien Swarm to C:\Program Files (x86)\Steam\steamapps\common\Alien Swarm\, modify run_client to point to the correct directory
7. Run the game via run_client.bat

## Building
1. Install Visual Studio 2013 (This is required even if you have a newer version of VS installed)
2. Go to src/ and run creategameprojects2013.bat
3. Click on Game_EP3-2013.sln
4. Change Debug to Release up the top
5. Build it from source

## TODO
* Make Test maps - join discord
* Fix blob rendering
* Improve icegun stability
* Remake models and assets

## Credits
* GaijinViking - Discovering the Episode 3 ice texture exists in Dota 2, and helping me find code to decompile.
* Klaxon - For the blobulator headers
* Wonderland War - For getting a recreation of the Episode 3 Weaponizer model made
* Valve - For making a great game, even if they did abandon it.
* OddDoc - Physics parameter file (that was a pain to work out, good thing he remade it) and blob materials


# OLD readme
This is my Half-Life 2 : Episode 3 restoration based on decompiled and leaked code for EP3. Some parts have been recreated, which are listed below

## Running
If you use Source Filmmaker and already have it installed, skip to step 2.
1. Install Source Filmmaker, move the Source Filmmaker hl2 folder in that installation to somewhere on your computer and then you can uninstall it
2. Install Alien Swarm
3. Download this project to some place on your hard drive
4. Change "E:\valve_asw_content\hl2" in (wherever you installed it)/game/ep3/gameinfo.txt to wherever that hl2 folder is (if you have Source Filmmaker and have not moved the hl2 folder, change it to the path of the one you have installed)
5. If you have not installed Alien Swarm to C:\Program Files (x86)\Steam\steamapps\common\Alien Swarm\, modify run_client to point to the correct directory
6. Run the game via run_client.bat

## Building
1. Install Visual Studio 2013 (This is required even if you have a newer version of VS installed)
2. Go to src/ and run creategameprojects2013.bat
3. Click on Game_EP3-2013.sln
4. Change Debug to Release up the top
5. Build it from source

## Features
* Blobulator
* Ice freezing code with Dota 2 ice texture
* Working Icegun recreation that can only freeze entities
* Early version of the blobulator blob & fountain present in the Source Engine 2007 leak restored to working condition (This version is after the Particle Benchmark version and before the final Episode 3 version)
* Combine Soldier Teleport Grenade throwing code
* Partial Margarita DDE decompilation
* Special Suit Ability weapon code (used in some ep3 weapons, though only weapon_icegun is known to use it)
* Final EP3 Blob func_pipe_entrance (though its mostly useless on its own)

## Recreated code
* weapon_teleport.cpp
* weapon_teleport.h
* weapon_icegun.cpp
* weapon_icegun.h


## TODO
* Fix blob demomonster/fountain rendering


## Credits
* GaijinViking - Discovering the Episode 3 ice texture exists in Dota 2, and helping me find code to decompile.
* Klaxon - For the blobulator headers
* Wonderland War - For getting a recreation of the Episode 3 Weaponizer model made
* Valve - For making a great game, even if they did abandon it.
