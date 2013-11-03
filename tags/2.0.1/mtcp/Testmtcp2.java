public class Testmtcp2 {
    private final static int COUNTINC = 10;

    private static int nproducers;

    public static void main (String[] argv) {
        nproducers = 3; // Integer.parseInt (argv[0]);

        for (int i = 1; i <= nproducers; i ++) {
            Thread producer = new ProducerThread (i);
            producer.start ();
        }

        new ProducerThread (0).run ();
    }

    private static class ProducerThread extends Thread {
        private int count;

        public ProducerThread (int c) {
            count = c;
        }

        public void run () {

            while (true) {
                count += COUNTINC;
                int sleepfor = (int) (Math.random () * 100.0);
                try {
                    Thread.sleep(sleepfor);
                } catch (InterruptedException ie) {
                }
                System.out.print (" " + count);
            }
        }
    }
}
