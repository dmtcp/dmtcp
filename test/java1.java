// Usage:  javac HelloWorldApp.java;
//         export CLASSPATH=<THIS_DIR>; java HelloWorldApp
// Note:  Sun/Oracle Java claims a huge amount of virtual memory
//   (mostly zero-mapped) for its object heap if not told otherwise.
// For DMTCP 1.2.3, call while setting maximum memory:
//   java -Xmx512M HelloWorldApp
// In the future, we will extend DMTCP to recognize zero-mapped memory
//   and to efficiently save and restore the zero-mapped pages,
//   using such techniques as /proc/*/smap and /proc/*/pagemap.

class java1 {
    public static void main(String argv[]){
        int iter = 0;
        System.out.println("Hello World!"); //Display the string.
	while (true) {
          iter++;
	  try{
	    Thread.currentThread().sleep(250);
	  } catch(InterruptedException ie) {}
          System.out.println("Hello World (continuing: " + iter + ").");
	}
    }
}
