public class Testmtcp5 {
    private final static int COUNTINC = 10;
    private final static int QUEUESIZE = 4;

    private static int babblesize;
    private static int nproducers;

    public static void main (String[] argv) {
        nproducers = 3; // Integer.parseInt (argv[0]);
        babblesize = 2 << 20; // Integer.parseInt (argv[1]) << 20;

        for (int i = 0; i < nproducers; i ++) {
            Thread producer = new ProducerThread ();
            producer.start ();
        }

        consumerfunc ();
    }

    private static Object indexmutex = new Object ();

    private static int producerindex = 0;
    private static int consumerindex = 0;
    private static int[] queuevalues = new int[QUEUESIZE];

    private static int threadcount = 0;
    private synchronized static int unique_count () {
        return (++ threadcount);
    }

    private static class ProducerThread extends Thread {
        public void run () {
            boolean queuewasempty, queuewasfull;
            byte[] babblebuff = new byte[babblesize];
            int count = unique_count ();
            int i, j;

            while (true) {
                count += COUNTINC;
                int sleepfor = (int) (Math.random () * 100.0);
                try {
                    Thread.sleep(sleepfor);
                } catch (InterruptedException ie) {
                }
/* too tuff for kaffe
                for (i = babblesize; -- i >= 0;) {
                    babblebuff[i] = (byte) (Math.random () * 255.0);
                }
*/
                do {
                    synchronized (indexmutex) {
                        i = producerindex;
                        j = (i + 1) % QUEUESIZE;
                        queuewasfull = (j == consumerindex);
                        if (!queuewasfull) {
                            queuevalues[i] = count;
                            producerindex = j;
                            queuewasempty = (i == consumerindex);
                            if (queuewasempty) {
                                indexmutex.notifyAll ();
                            }
                        } else {
                            try {
                                indexmutex.wait ();
                            } catch (InterruptedException ie) { }
                        }
                    }
                } while (queuewasfull);
            }
        }
    }


    private static void consumerfunc () {
        boolean queuewasempty, queuewasfull;
        int i, j;
        int value = 0;

        while (true) {
            synchronized (indexmutex) {
                i = consumerindex;
                queuewasempty = (i == producerindex);
                j = (producerindex + 1) % QUEUESIZE;
                queuewasfull = (j == i);
                if (!queuewasempty) {
                    value = queuevalues[i];
                    consumerindex = (i + 1) % QUEUESIZE;
                } else {
                    try {
                        indexmutex.wait ();
                    } catch (InterruptedException ie) { }
                }
                if (queuewasfull) {
                    indexmutex.notifyAll ();
                }
            }
            if (!queuewasempty) {
                System.out.print(" " + value);
                System.out.flush();
            }
        }
    }
}
