# PopVend
PopVend is a 3D printed mini vending machine that uses a trigr.dev QR code to accept instant card payments.

![popvend-hero](assets/images/PopVendHero.png)

## Features

* 6 unique item slots that can fit things about the height and length of a credit card.
* Fully electronic and automatic vending
* Small front screen with customizable scolling idle text
* Customizable RGB LEDs above each product
* QR code payments using [trigr.dev](https://trigr.dev) - Triggers vends instantly after online payments, and payment go instantly to your own stripe account  

To print the parts you will need access to a 3D printer that can print up to 250mm high and 142mmx215mm

The full build is designed for makers that have some knowledge of electronics, soldering skills and a basic understanding of the Arduino IDE. However, pre soldered circuit boards and some helpful kits will be available on my [kofi page](https://ko-fi.com/shop/settings?src=sidemenu&productType=0) that will make the build much much easier.

## How To Build

Full build tutorial coming soon

## How to use Stripe and Trigr for QR code payments

Once your vending machine is all put together and working, it's time to get it setup to vend automatically with Stripe payments. Luckily, this is made very easy with [Trigr.dev](https:/trigr.dev) . 

### Step 1: Make a Trigr account & connect your stripe account

Go HERE to create an a free account on trigr. Once your account is setup, you will need to follow the instructions to verify your email. Once your email has been verified, you will be able to onboard your stripe account. To on board your Stripe account click the yellow start button at the top of your Trigr dashboard:

![popvend-hero](assets/images/TrigrOnboard1.png)

This will launch the official Stripe onboarding process. If you already have Stripe account that you want to use, thats okay, just use that information and it will just make onboarding faster.

![popvend-hero](assets/images/StripeOnboarding.png)

Sometimes it takes a few hours for your info to be processed and approved. You can check your Stripe connection status on you "Account" tab:

![popvend-hero](assets/images/connected.png)

### Step 2: Create your Checkout Portal and get your Device token

Next, click on the "Portals" tab in your trigr dashboard, and click "+ New Portal". This is where you will specify what products you are selling in your vending machine. 

![popvend-hero](assets/images/newPortal.png)

Add a product for ALL 6 shelves in the machine, even if they are the same product.

![popvend-hero](assets/images/PortalCreate.png)

Once your portal is made, go to the "Devices" tab to create your device. During device creation, use the "portal" dropdown to select the portal you just created.

![popvend-hero](assets/images/createDevice.png)

Once that is done, your Trigr portal is all setup to accept payments from your vending machine. However, keep this page open because you will need it one more time in the final step.


### Step 3: Copy your Trigr tokens into your firmware, and print your QR code

Just a few more things before you're ready to display your machine.

First, you will need to copy some of the tokens that are now available in your trigr account into your Arduino firmware to flash onto your esp32. Directly at the top of the arduino firmware provided in '/firmware' are a bunch of configurable parameters to customize your vending machine: 

![popvend-hero](assets/images/Tokens.png)

Here you can customize the LED colors and the scroll text, as well as setup your WIFI credentials, and trigr tokens.

To get your trigr device token, go back to the "devices" tab on your trigr dashboard. Just click on the "copy device ID" button and paste it into the Arduino IDE:

![popvend-hero](assets/images/deviceId.png)

Next you will need to add a token for each SHELF in the vending machine (Each token should be unique, even if you are selling the same thing on multiple shelves... just make a separate product for each shelf). To do that go back to the "Portals" tab and find the portal you setup with your products. There is a "copy" button displayed next to each product in the portal, there should always be 6 of these when setting up a PopVend. Copy each one over to the firmware, replacing the text between the quatations marks on lines 19 through 24.

And thats is! once you re-flash your board with that firmware, you should see the device come online on your Trigr portal. To test your machine, go back to the devices tab and click the "Admin" button on your device. This will take you to a checkout page similar to what a customer will see when they scan your device QR code. Here you can click on each product and simulate a payment going through by clicking "Trigger Payment Confirmed".

One last thing you'll want to do is go back to the device tab and click the "QR code" button. This will generate a QR code specifically for your device that you can download to print and mount on your vending machine.


---
*Created by [devmode](https://www.devinjames.tech/). Need help with your build? Join the [discord server](https://discord.gg/Y6D79bpH)!