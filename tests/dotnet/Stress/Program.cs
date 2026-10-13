using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using CommunicationDriver;

static class Program {
 static readonly string Ready=RuntimeInformation.IsOSPlatform(OSPlatform.Windows)?"echo|set /p=READY":"printf READY";
 static void Expect(bool value,string message){if(!value)throw new Exception(message);}
 static async Task Bounded(Task task){await task.WaitAsync(TimeSpan.FromSeconds(15));}
 static async Task Observe(Task task){try{await task;}catch(CommException e){Expect(e.Error.Code!=ErrorCode.InternalError,"native internal error during cancellation");}catch(CommOperationCanceledException){} }
 static async Task Storm(){
  using var driver=new Driver();using var connection=driver.CreateCli(new CliOptions());await connection.OpenAsync();
  using var cancel=new CancellationTokenSource();
  var producers=Enumerable.Range(0,8).Select(_=>Task.Run(async()=>{
   var tasks=new List<Task>();
   for(int i=0;i<500;++i){tasks.Add(Observe(connection.ExecuteAsync(Ready,cancellationToken:cancel.Token)));if(i==100)cancel.Cancel();}
   await Task.WhenAll(tasks);
  })).ToArray();
  GC.Collect();GC.WaitForPendingFinalizers();await Bounded(Task.WhenAll(producers));
  await connection.CloseAsync();await connection.OpenAsync();
  Expect((await connection.ExecuteAsync(Ready)).Response=="READY","recovery after managed cancellation storm");
  await Bounded(Task.WhenAll(Enumerable.Range(0,128).Select(_=>connection.CloseAsync())));
 }
 [MethodImpl(MethodImplOptions.NoInlining)]
 static WeakReference[] Churn(){
  var driver=new Driver();var connection=driver.CreateCli(new CliOptions());connection.Open();
  var refs=new[]{new WeakReference(driver),new WeakReference(connection)};connection.Dispose();driver.Dispose();return refs;
 }
 static async Task Main(string[] args){
  int rounds=args.Length==0?10:int.Parse(args[0]);var begin=Stopwatch.StartNew();
  await Storm();GC.Collect();GC.WaitForPendingFinalizers();long baseline=GC.GetTotalMemory(true);
  var weak=new List<WeakReference>();
  for(int i=0;i<rounds;++i){await Storm();weak.AddRange(Churn());}
  GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();
  Expect(weak.All(reference=>!reference.IsAlive),"disposed wrappers must not remain rooted");
  Console.WriteLine($"PASS managed_stress operations={(rounds+1)*4000} concurrent_closes={(rounds+1)*128} collected_wrappers={weak.Count} baseline_gc_bytes={baseline} final_gc_bytes={GC.GetTotalMemory(true)} elapsed_ms={begin.ElapsedMilliseconds}");
 }
}
