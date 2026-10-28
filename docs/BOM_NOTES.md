# Stardance BOM notes (drafted 2026-10-28)

Priced total for one device: $59.20 (priced lines only). Cap is $120, so it is under, with $60.80 headroom for the unpriced lines below.

## Could not verify (price cells left empty)
- J1 LiPo 3.7 V 1200 mAh 40x30x10 mm (103040): no USD listing found. Only probots.co.in INR 269 incl GST, not converted. Micro Center's page showed $999 for a different (Adafruit 258) cell; ignored as an error.
- PCB shipping share: no JLCPCB rate readable without uploading Gerbers and choosing a destination.
- 3D printed case filament: not looked up.
- Order shipping and import duties to Indonesia for all vendors (LCSC, DFRobot, ProtoSupplies, RobotShop, Walmart, PetHardware): not looked up.
- Tokopedia/Shopee/AliExpress prices: none read (pages not fetchable), so no clone-price for the DFPlayer clone or the GY-521.

## Prices that are substitutes or upper bounds
- U3: GY-521 priced with DFRobot SEN0142 ($12.90), a different MPU-6050 board. Real GY-521 clones are far cheaper (EUR 3.41 seen, sold out) but not verified in USD.
- U2: genuine DFRobot DFPlayer Mini ($5.90) used instead of a clone.
- U2 microSD: $14.00 RobotShop, includes tariffs; very likely cheaper locally.
- Screws: priced as a whole 55-pack ($10.09). Split ring: min order is 10 ($9.00 real outlay), counted at 1 pc ($0.90).

## Part or fit mismatches to check
- Q1: LCSC's FS8205A listing (C908265) is SOT-23-6L, not TSSOP-8. I used a TSSOP-8 "8205A" (HL, C7499846). Confirm acceptable.
- SW1-SW3: SparkFun COM-10791 right-angle tact is a price-verified candidate only. No dimensions on page; check ~11.7 mm actuator height and footprint.
- SW4 listed 6x6x5 mm vs ~5.3 mm in notes. SW5 MSK12C02 footprint and side actuation unconfirmed.
- J3: PJ-307N5 pole count not stated on LCSC page; pinout/footprint must match the PJ-307 on the board.
- All 0603 passives: package size assumed, not read from the schematic.
- 10k 0603 (C25804) showed out of stock; 100nF (C1591) page contradicted itself on stock and price.
- Heat-set inserts (M2 x 5 x 3.5) must match the case CAD.
- PCB: $2 for 5 boards comes from Hack Club's JLC guide (under 100x100 mm, 2-layer). A 1.2 mm thickness surcharge was not checked. The guide also mentions JLC tariffs (60%) at order time; not applied here.

## LCSC price caveat
Many LCSC prices are the "from" figure on the page; the single-piece price may be higher (DW01A and SS14 are 20+ / 5+ tiers). The difference is cents. LCSC minimum order and shipping are the real cost.

## Added by BOM drafter, confirm against schematic
- LED1, LED2 (red 0603) and two 1k resistors for TP4056 CHRG/STDBY.
- C7 10uF bulk cap on VSYS for DFPlayer current spikes.
- H1 1x40 header strip for DFPlayer, GY-521, display (RP2040-Zero is castellated).
- Not added: I2C pull-ups (GY-521 has onboard ones, value unchecked), button pull-ups (assumed internal), second divider resistor (both 100k already listed).
