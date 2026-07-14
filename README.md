# TwinOak Standard Repeater Design v2

## General design concept and project goals
The thought behind this project came to life after quite a few failures in OTA firmware updates. There was need for a better, and more stable, way of doing the firmware updates. 
Besides the devices being bricked from the OTA process, repeaters can also sit quite a long distance away, so it would also be desireable to have something that could update remotely, from the comfort of a desk. 
One could consider using LoRa for sending the updates to the repeaters, but since the network is already congested enough, it seems like a bad idea to pile even more load on that band, even assuming that it somehow would be possible to get a stable link to all the repeater sites.

It was decided to go for a design which incorporated an LTE-M module for managinge the node. This gives almost 100% coverage(in Denmark at least!) and high bandwidth for firmware transfers. 
The structure looked something like this:
```mermaid
flowchart LR
    subgraph Mesh plane
    A@{ shape: cloud, label: "Mesh network" } <--> B[LoRa module]
    end
    B[LoRa module] <-.-> C[LTE-M module]
    subgraph Management plane
    C <--> D@{ shape: cloud, label: "Internet" }
    end
```

Besides the problematic management of remote nodes, there is also a few other features to address with this new repeater design. This meant that the primary goals for the project ended up being:
1. <b>Remote management</b>. Being able to do management remotely based on existing LTE infrastructure. Only management, no data via LTE.
2. <b>Superior noise "rejection"</b> with radio in RF shielding and using proper cables and isolation to give the LoRa the best possible noise floor. Previous experience with SenseCAP Solar P1 nodes, even though they are very nice nodes, proved that they are just plastic enclosures with no RF protection. Put them next to a cell tower and you are in trouble! A bit ironic that the biggest nemesis regarding noise in MeshCore repeaters is LTE, when this going to use LTE as the transport for the management plane!
3. <b>Integrated cavity filter</b>. There has been multiple issues with noise in all many locations. There is always a cell tower(or some DAB/DVT/whatever tower) sitting just next to great locations. The Solar P1 only offers the option to add a small SAW filter, but one goal of this project is that it should to be easy to add a good cavity filter. Even maybe be the default.
4. <b>Modularity</b>. Things change fast; firmware, features and other software changes fast! But hardware also changes. With the Solar P1 one would have to replace the entire unit if a new and better radio became available. This project should solve this by enabling changing JUST the radio or JUST the solarpanel or JUST the batteries. Not doing a complete "forklift upgrade" everytime is a must.

## Hardware design principle
The hardware is split into 3 parts:
1. The LoRa part
2. The power part
3. The management part

To make these function together in a modular way each part has been split into multiple "levels" with increasing amount of specialization the higher the level. This has been illustrated here:
```mermaid
block
  columns 7
  block:stuff:2
    columns 1
    A1["LoRa modul"] 
    space
    A2["LoRa adapter board"] 
    space
    A3["Platform board"]
    space
    space
  end
  space
  block:stuff2:2
    columns 1
    space
    space
    space
    space
    B3["Platform mgmt"]
    space
    B4["Power"]
  end
  space
  block:stuff3:2
    columns 1
    C1["LTE modul"] 
    space
    C2["LTE adapter board"]
    space
    C3["Platform board"]
    space
    space
  end

  %% Arrows vertically
  A1 <--> A2
  A2 <--> A3

  B4 --> B3

  C1 <--> C2
  C2 <--> C3

  %% Arrows horisontally
  A3 <--> B3
  B3 <--> C3
```
From the bottom up:
- <b>Power</b> is just that: Simple DC power source from either solarpanel, grid connection or battery.
- <b>Platform</b> and <b>Platform management</b> is introducing management of the power from the power source and handling signaling between the two platform boards. Each platformboard is just supplying simple power to the upper levels and generic IO between the levels.
- <b>Adapters</b> change the generic IO and power from the platform to the specific needs of the module above it. That could be board specific power management, levelshifting of IO signals and routing to the proper pins for the given module above.
- <b>Radio modules</b> is the actual off-the-shelf radio modules, such as a Heltec LoRa module like the T096 or similar.

Each of the 3 parts should be kept as RF isolated as possible as the two radio modules will likely interfer with each other. So the parts also indicate an "physical seperation" with RF shielding. To make this as good as possible the entire repeater should be house inside a RF shielded enclosure and both the LoRa part and the management part should be kept in seperate RF shielded enclosures. Great care should be taken at the interfaces between each part, such that RF noise don't traverse the bounderies between each part.


## Bill-of-materials and links to sources

| Component            | Brand      | Model                             | Source                                                                                                   | Price     |
|----------------------|------------|-----------------------------------|----------------------------------------------------------------------------------------------------------|----------:|
| <u>*External components*</u>                                                                                                                                                                 |
| Antenna, LoRa        | Vinnant    | CC868/8-PEL 10dBi                 | [Hoeg teknik](https://hoegteknik.dk/shop/da/dv-nodermeshtastic/483-8dbd-58-antenne-for-868-mhz.html)     |   599 DKK |
| Antenna, LTE-M       |            | IP67, N-Male, "9dBi"              | [Aliexpress](https://www.aliexpress.com/item/1005005889564983.html)                                      |   ~50 DKK |
| Cable                | Bevotop    | LMR240, N Male to N Male, 80cm    | [Aliexpress](https://www.aliexpress.com/item/1005002640557645.html)                                      |   ~70 DKK |
| Pipe                 |            | 1" pipe, 200cm                    | [Webshop](https://shop.erik-larsen.dk/products/pipe-galv?variant=39625182412883)                         |   150 DKK |
| Brackets             |            | Pipeclamps, set of 2, max 60mm    | [Amazon](https://www.amazon.de/-/da/Premium-mastklemme-dobbeltklemme-galvaniseret-dobbelt/dp/B010UL5B66) |   ~67 DKK |
| Enclosure            |            | Diecast alu box, IP67             | [Aliexpress](https://www.aliexpress.com/item/1005007492347455.html)                                      |  ~185 DKK |
| Solar panel          |            | "30W" Solar Panel                 | [Aliexpress](https://www.aliexpress.com/item/1005009053070915.html)                                      |  ~200 DKK |
| Vent valve           |            | M16 watertight alu vent valve     | [Aliexpress](https://www.aliexpress.com/item/1005012242062756.html)                                      |   ~20 DKK |
| Bracket              |            | For solar panel                   | TBD                                                                                                      |           |
| Bracket              |            | For enclosure                     | TBD                                                                                                      |           |
| <u>*Internal components*</u>                                                                                                                                                                 |
| Backplates           | Custom     | Set of 2pce, 1,5mm lasercut alu   | (Fusion link? DXF?)                                                                                      |   280 DKK |
| RF boxes             | Gainta     | G103-IP67, 64x98x34mm, alu, 2pce  | [TME](https://www.tme.eu/dk/en/details/g103-ip67/multipurpose-enclosures/gainta/)                        |   108 DKK |
| Cavity filter        | Sysmocom   | 7Mhz Cavity filter(Link to test?) | [Sysmocom](https://shop.sysmocom.de/868-863..870-MHz-cavity-filter-ISM-LoRa-SigFox-Helium/cf866.5-kt30)  |  ~400 DKK |
| Battery              | Meshnology | 10.000mA LiPo battery             | [Meshnology](https://meshnology.com/collections/frontpage/products/3-7v-10000mah-lipo-battery-with-usb-charger-protection-for-arduino-esp32-drone?variant=46431266963694) | ~150 DKK |
| Cable                |            | U.FL -> SMA pigtail, 10cm         | [Aliexpress](https://www.aliexpress.com/item/1005009937311685.html)                                      |   ~10 DKK |
| Cable                |            | N-female -> SMA pigtail, 10cm     | [Aliexpress](https://www.aliexpress.com/item/1005008984322302.html)                                      |   ~10 DKK |
| Cable                |            | N-female -> SMA pigtail, 20cm     | [Aliexpress](https://www.aliexpress.com/item/1005008984322302.html)                                      |   ~10 DKK |
| Cable                |            | SMA -> SMA pigtail, 10cm          | [Aliexpress](https://www.aliexpress.com/item/1005006890065800.html)                                      |   ~10 DKK |
| <u>*Radio modules*</u>                                                                                                                                                                       |
| LoRa module          | Heltec     | Heltec V3, ESP32 based            | [Heltec](https://heltec.org/project/wifi-lora-32-v3/)                                                    |  ~120 DKK |
| *...or...*                                                                                                                                                                                   |
| LoRa module          | Heltec     | Heltec T096, nRF52 based          | [Heltec](https://heltec.org/project/t096/)                                                               |  ~230 DKK |
| LTE module           | Quickspot  | Walter LTE MCU, ESP32 based       | [TinyTronics](https://www.tinytronics.nl/nl/development-boards/microcontroller-boards/met-telecommunicatie/walter-esp32-s3-iot-development-board-cat-m-nb-iot-en-gnss-gm02sp) |   ~510 DKK |
| <u>*Custom PCBs*</u>                                                                                                                                                                         |
| Platform board       |            | TBD                               |                                                                                                          |           |
| Heltec V3 adapter    |            | TBD                               |                                                                                                          |           |
| *...or...*                                                                                                                                                                                   |
| Heltec T096 adapter  |            | TBD                               |                                                                                                          |           |
| LoRa connector board |            | TBD                               |                                                                                                          |           |
| LTE connector board  |            | TBD                               |                                                                                                          |           |
| <u>*Electrical components*</u>                                                                                                                                                               |
| D-sub connector      | Amphenol   | FCE17-E09SM-250, 9-pin, filter, x2| [Mouser](https://eu.mouser.com/en/ProductDetail/Amphenol-Commercial-Products/FCE17-E09SM-250)            |  ~175 DKK |
| D-sub connector      | Amphenol   | L717DFE09PT, 9-pin, male PCB, x2  | [Mouser](https://eu.mouser.com/en/ProductDetail/Amphenol-Commercial-Products/L717DFE09PT)                |  ~100 DKK |
| Capacitor            | Panasonic  | 16SEPG470M, 470µF PolyAlu, x1     | [TME](https://www.tme.eu/dk/en/details/16sepg470m/tht-polymer-capacitors/panasonic/)                     |   ~10 DKK |
| Capacitor            | AISHI      | EWH1CM101E11OT, 100µF, x2         | [TME](https://www.tme.eu/dk/en/details/ce-100_16pht-y/tht-electrolytic-capacitors/aishi/ewh1cm101e11ot/) |    ~1 DKK |
| Capacitor            | SR passives| CCT-10N/100V-S, 10nF Ceramic, x2  | [TME](https://www.tme.eu/dk/en/details/cct-10n_100v-s/mlcc-tht-capacitors/sr-passives/ct40805b103k101f1r/) |  ~1 DKK |                                                                                                        |           |
| Connector            |            | Pin headers for radio modules?    | TBD                                                                                                      |           |
| Connector            |            | Low profile headers for adapters? | TBD                                                                                                      |           |

Total: ~3.250DKK + TBD + solder/heatshrink/screws/wires/etc + shipping/customs/fees/etc

## LTE-M providers

### Lebara
Lebara SIM card, their cheapest plan, has been tested and it worked fine with the Walter MCU and could get contact with the internet without issues.

### NexCon
The LTE provider nexcon.io has been tested. They have a 25MB/month plan that is VERY cheap, like 1eur/month per SIM card including a management website that is VERY nice! Can highly recommend them, however they might only serve commercial customers, so check that first.