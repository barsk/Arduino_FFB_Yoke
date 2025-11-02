from PyQt6 import QtWidgets, QtGui, uic
from PyQt6.QtCore import QCommandLineOption, QCommandLineParser, QCoreApplication, QUrl
from PyQt6.QtGui import QDesktopServices
import serial, sys, os, time, struct
# import win32com.client
from serial.tools.list_ports import comports

basedir = os.path.dirname(__file__)

try:
    from ctypes import windll  # Only exists on Windows.
    myappid = 'com.siminvent.ffbyoke.yoketool.0.9'
    windll.shell32.SetCurrentProcessExplicitAppUserModelID(myappid)
except ImportError:
    pass

class MainWindow(QtWidgets.QMainWindow):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs) 

        # Well this is clumsy, but I could not figure out how to pass QtCommandLineParser here...
        # So for the time being, this is it.
        self.spyMode = False
        if (len(QCoreApplication.arguments()) > 1 and (QCoreApplication.arguments()[1] == "-s" or QCoreApplication.arguments()[1] == "--spy")):
            self.spyMode = True

        # Serial port instance placeholder
        self.ser = None

        #Load the UI Page
        uic.loadUi(os.path.join(basedir, "mainwindow.ui"), self)

        self.setWindowTitle("SimInvent FFB Yoke Tool v0.9")
        
        self.scanUsbPorts()
        self.pushButton_scanPorts.clicked.connect(self.scanUsbPorts)
        self.pushButton_connect.clicked.connect(self.connectSerial)
        self.pushButton_disconnect.clicked.connect(self.disconnectSerial)
        self.pushButton_read.clicked.connect(self.readSettings)
        self.pushButton_ok.clicked.connect(self.handleOk)
        self.pushButton_apply.clicked.connect(self.writeSettings)
        self.pushButton_reset.clicked.connect(self.resetDevice)

        self.actionWeb_site.triggered.connect(self.openWebSite)
        self.actionDocumentation_wiki.triggered.connect(self.openWiki)

        # Hide read button, only used during debugging
        self.pushButton_read.hide()

    def scanUsbPorts(self):
        # wmi = win32com.client.GetObject ("winmgmts:")
        # for usb in wmi.InstancesOf ("Win32_USBHub"):
        #     print(usb.DeviceID)
        self.comboBox_usbPort.clear()
        com_ports = comports() # create a list of com ['COM1','COM2'] 
        com_ports.sort()
        for i in com_ports:            
            # print(i.name) # returns 'COMx'   
            self.comboBox_usbPort.addItem(i.name, i.name)

    def connectSerial(self):
        portName = self.comboBox_usbPort.currentData()
        try:
            if (self.spyMode):
                self.ser = serial.serial_for_url("spy://" + portName, do_not_open=True)
            else:
                self.ser = serial.Serial()
                self.ser.port = portName
            
            self.ser.baudrate=115200
            self.ser.parity=serial.PARITY_NONE
            self.ser.stopbits=serial.STOPBITS_ONE
            self.ser.bytesize=serial.EIGHTBITS
            self.ser.timeout=1
            self.ser.write_timeout=1
            self.ser.open()
            self.pushButton_read.setEnabled(True)
            self.statusbar.showMessage(f"{portName} connected", 30000)

            self.readSettings()        
        except serial.SerialException as err:
            self.disconnectSerial(); 
            self.statusbar.showMessage(f"{portName} serial error: {err}", 0)
        
    def disconnectSerial(self):
        self.ser.close()
        self.tabWidget.setEnabled(False)
        self.pushButton_read.setEnabled(False)
        self.pushButton_apply.setEnabled(False)
        self.pushButton_ok.setEnabled(False)
        self.pushButton_reset.setEnabled(False)

        self.pushButton_disconnect.setEnabled(False)
        self.pushButton_connect.setEnabled(True)
        self.comboBox_usbPort.setEnabled(True)
        self.pushButton_scanPorts.setEnabled(True)
        self.statusbar.showMessage(f"{self.ser.port} disconnected", 0)

    # Read settings from device
    def readSettings(self):       
        try:
            self.ser.reset_input_buffer(); # purge possible old buffer data
            self.ser.write(b'<TX>') # Ask device to transmit settings as packed byte stream

            # struct format on device side (c struct), START char (0x2)+'<', 7 bytes, END char (0x3) + '<'
            # However, the START byte is searched for and read before unpacking
            structFormat = '7Bcc' 
            structSize = struct.calcsize(structFormat)

            # Search START of packet (2 bytes)
            self.ser.read_until(b'\2<') 
            data = self.ser.read(structSize)
            tup = struct.unpack(structFormat, data)  #struct.unpack(format, bytestring)

            #debug
            for i in range(structSize):
                print(f"{i}: {tup[i]}")

            if (ord(tup[7]) == 3 and ord(tup[8]) == 62): # check proper padding END ASCII char + '<' for packet       
                self.slider_rollGain.setValue(tup[0])
                self.slider_rollPwmMin.setValue(tup[1])
                self.slider_pitchGain.setValue(tup[2])
                self.slider_pitchPwmMin.setValue(tup[3]) 
                self.slider_pitchTravel.setValue(tup[4])
                self.slider_defaultSpringGain.setValue(tup[5])
                self.slider_maxVelocityPcnt.setValue(tup[6])

                self.tabWidget.setEnabled(True)
                self.pushButton_apply.setEnabled(True)
                self.pushButton_ok.setEnabled(True)
                self.pushButton_reset.setEnabled(True)

                self.pushButton_disconnect.setEnabled(True)
                self.pushButton_connect.setEnabled(False)
                self.comboBox_usbPort.setEnabled(False)
                self.pushButton_scanPorts.setEnabled(False)

                self.statusbar.showMessage(f"Connected {self.ser.port}, device settings read", 0)
            else:
                self.statusbar.showMessage(f"Illegal packet data! Skipping!", 5000)
        except struct.error as err:
            self.statusbar.showMessage(f"Serial packet error: {err}", 5000)
        except serial.SerialException as err:
            self.disconnectSerial()
            self.statusbar.showMessage(f"Device not responding! Error: {err}", 5000)
    
    # Write settings to device
    def writeSettings(self):
        try:
            self.ser.write(b'<RX>') # ask device to receive settings as packed bytes (c struct)

            # struct format on device side (c struct), START byte (0x2), 7 bytes, END byte (0x3)
            structFormat = 'cc7Bcc' 

            # Pad packet with ASCII controls char START/END
            data = struct.pack(structFormat, 
                b'\2', b'<',
                self.slider_rollGain.value(),
                self.slider_rollPwmMin.value(),
                self.slider_pitchGain.value(),
                self.slider_pitchPwmMin.value(),
                self.slider_pitchTravel.value(),
                self.slider_defaultSpringGain.value(),
                self.slider_maxVelocityPcnt.value(),
                b'\3', b'>',
                )
            self.ser.write(data)
            self.ser.flush()
            self.statusbar.showMessage(f"Settings written", 3000)
        except serial.SerialException as err:
            self.disconnectSerial()
            self.statusbar.showMessage(f"Error: {err}", 0)

    # Read settings from device
    def resetDevice(self):
        try:
            self.ser.reset_input_buffer(); # purge possible old buffer data
            self.ser.write(b'<RS>') # Ask device to RESET
            self.ser.flush()
            time.sleep(0.25) # Let device respond. 
            self.readSettings()
            self.statusbar.showMessage(f"Device reset!", 3000)
        except serial.SerialException as err:
            self.disconnectSerial()
            self.statusbar.showMessage(f"Error: {err}", 0)

    def handleOk(self):
        self.writeSettings()
        self.close()

    def openWebSite(self):
        QDesktopServices.openUrl(QUrl("https://github.com/barsk/Arduino_FFB_Yoke"))

    def openWiki(self):
        QDesktopServices.openUrl(QUrl("https://github.com/barsk/Arduino_FFB_Yoke/wiki"))
    

def parse(app):
    """Parse the arguments and options of the given app object."""
    parser = QCommandLineParser()
    # parser.addHelpOption()

    spy_option = QCommandLineOption(
        ["s", "spy"],
        "Spy on the serial port transmission"
    )
    parser.addOption(spy_option)
    parser.process(app)
    
          
def main():
    app = QtWidgets.QApplication(sys.argv)
    app.setApplicationName("SimInvent FFB Tool")
    app.setWindowIcon(QtGui.QIcon(os.path.join(basedir, 'SimInvent.ico')))
    parse(app)
    main = MainWindow()
    main.show()
    main.setFixedHeight(main.height())
    main.setFixedWidth(main.width())

    sys.exit(app.exec())

if __name__ == '__main__':
    main()