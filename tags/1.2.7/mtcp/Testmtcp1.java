public class Testmtcp1 {
    public static void main (String[] argv) {
        int count = 0;
        while (true) {
           ++ count;
           System.out.print (" " + count);
           try {
               Thread.sleep (1000);
           } catch (InterruptedException ie) { }
        }
    }
}
