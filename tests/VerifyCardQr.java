import java.awt.image.BufferedImage;
import java.io.File;
import javax.imageio.ImageIO;
import com.google.zxing.*;
import com.google.zxing.common.HybridBinarizer;

class VerifyCardQr {
    static String decode(BufferedImage image) throws Exception {
        int w = image.getWidth(), h = image.getHeight();
        byte[] pixels = new byte[w * h];
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int rgb = image.getRGB(x, y);
            pixels[y * w + x] = (byte)((((rgb >> 16) & 255) + 2 * ((rgb >> 8) & 255) + (rgb & 255)) / 4);
        }
        LuminanceSource source = new LuminanceSource(w, h) {
            public byte[] getMatrix() { return pixels; }
            public byte[] getRow(int y, byte[] row) {
                if (row == null || row.length < w) row = new byte[w];
                System.arraycopy(pixels, y * w, row, 0, w);
                return row;
            }
        };
        var hints = new java.util.EnumMap<DecodeHintType,Object>(DecodeHintType.class);
        hints.put(DecodeHintType.TRY_HARDER, true);
        hints.put(DecodeHintType.PURE_BARCODE, true);
        hints.put(DecodeHintType.POSSIBLE_FORMATS, java.util.List.of(BarcodeFormat.QR_CODE));
        var bitmap = new BinaryBitmap(new HybridBinarizer(source));
        return new com.google.zxing.qrcode.QRCodeReader().decode(bitmap, hints).getText();
    }
    public static void main(String[] args) throws Exception {
        BufferedImage card = ImageIO.read(new File(args[0]));
        String expected = "ocp1://pair?h=192.168.1.10&p=47100&s=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef&n=OBS";
        if (!expected.equals(decode(card.getSubimage(718,95,530,530)))) throw new AssertionError("Full-size QR payload mismatch");
        BufferedImage small = new BufferedImage(640, 360, BufferedImage.TYPE_INT_RGB);
        var graphics = small.createGraphics();
        graphics.drawImage(card, 0, 0, 640, 360, null);
        graphics.dispose();
        if (!expected.equals(decode(small.getSubimage(359,48,265,264)))) throw new AssertionError("Scaled QR payload mismatch");
        System.out.println("OBS pairing card QR decoded correctly at 1280x720 and 640x360");
    }
}
