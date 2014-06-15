import Image
import gameduino2 as gd2

class Interface(gd2.prep.AssetBin):
	def addall(self):
		interface = gd2.prep.split( 30, 157, Image.open("assets/InterfaceElements.png") )
		self.load_handle("INTERFACE", interface, gd2.RGB565 )
		axes = gd2.prep.split( 106, 27, Image.open("assets/ChartElements.png") )
		self.load_handle("AXES", axes, gd2.RGB565 )
		bigNumbers = gd2.prep.split( 48, 32, Image.open("assets/BigNumbers.png") )
		self.load_handle("BIG_NUMBERS", bigNumbers, gd2.RGB565 )
		signs = gd2.prep.split( 48, 40, Image.open("assets/BigSigns.png") )
		self.load_handle("BIG_SIGNS", signs, gd2.RGB565 )
		smallNumbers = gd2.prep.split( 19, 13, Image.open("assets/SmallNumbers.png") )
		self.load_handle("SMALL_NUMBERS", smallNumbers, gd2.RGB565 )
		smallSigns = gd2.prep.split( 21, 16, Image.open("assets/SmallSigns.png") )
		self.load_handle("SMALL_SIGNS", smallSigns, gd2.RGB565 )
		settings = gd2.prep.split( 16, 16, Image.open("assets/Icons.png") )
		self.load_handle("ICONS", settings, gd2.RGB565 )
if __name__ == '__main__':
    Interface().make()