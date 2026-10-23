#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <unistd.h>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Standalone experiment: never injected into a game, never selects CPU fallback
// when testing HTP/GPU. Run time includes ORT execution and its graph I/O work,
// but not Vulkan readback/upload, render work, presentation or input latency.
int main(int argc,char** argv){
 try{
  if(argc<3)throw std::runtime_error("Usage: thor-onnx-probe MODEL htp|gpu|cpu [INPUT.f32 OUTPUT.f32 PROFILE_PREFIX]");
  using Clock=std::chrono::steady_clock;
  auto start=Clock::now();
  const std::string backend=argv[2];
  if(backend!="htp"&&backend!="gpu"&&backend!="cpu")throw std::runtime_error("Unknown backend");
  auto providers=Ort::GetAvailableProviders();for(const auto& provider:providers)printf("ORT_PROVIDER %s\n",provider.c_str());
  Ort::Env env(ORT_LOGGING_LEVEL_VERBOSE,"ThorNeuralProbe");
  Ort::SessionOptions options;options.SetIntraOpNumThreads(1);options.SetInterOpNumThreads(1);
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  if(argc>5)options.EnableProfiling(argv[5]);
  if(backend!="cpu"){
   options.AddConfigEntry("session.disable_cpu_ep_fallback","1");
   std::unordered_map<std::string,std::string> config{
    {"backend_path",backend=="htp"?"/data/local/tmp/thor-neural/libQnnHtp.so":"/data/local/tmp/thor-neural/libQnnGpu.so"},
    {"offload_graph_io_quantization","0"}
   };
   if(backend=="htp"){
    config["enable_htp_fp16_precision"]="1";
    config["htp_performance_mode"]="balanced";
   }
   options.AppendExecutionProvider("QNN",config);
  }
  printf("ORT_SESSION_BEGIN pid=%d backend=%s cpu_fallback=%d\n",getpid(),backend.c_str(),backend=="cpu");fflush(stdout);
  Ort::Session session(env,argv[1],options);
  double sessionMs=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
  auto inputInfo=session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
  bool half=inputInfo.GetElementType()==ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16;
  if(!half&&inputInfo.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)throw std::runtime_error("Unsupported input type");
  auto shape=inputInfo.GetShape();auto outputShape=session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
  if(shape.size()!=4||shape[0]!=1||shape[1]!=3||shape[2]<1||shape[3]<1||shape[2]>1080||shape[3]>1920)
   throw std::runtime_error("Unexpected NCHW input shape");
  if(outputShape!=std::vector<int64_t>({1,3,shape[2]*2,shape[3]*2}))throw std::runtime_error("Unexpected output shape");
  std::vector<float> input(size_t(shape[1]*shape[2]*shape[3]));
  for(size_t i=0;i<input.size();i++)input[i]=float((i*13+i/97)%256)/255;
  if(argc>3){std::ifstream file(argv[3],std::ios::binary);file.read((char*)input.data(),input.size()*sizeof(float));if(!file||file.peek()!=EOF)throw std::runtime_error("Input fixture size mismatch");}
  auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
  std::vector<Ort::Float16_t> inputHalf;if(half){inputHalf.reserve(input.size());for(float x:input)inputHalf.emplace_back(x);}
  auto tensor=half?Ort::Value::CreateTensor<Ort::Float16_t>(memory,inputHalf.data(),inputHalf.size(),shape.data(),shape.size()):Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),shape.data(),shape.size());
  const char* inputs[]={"image"};const char* outputs[]={"output"};Ort::RunOptions run;
  std::vector<double> samples;std::vector<Ort::Value> result;
  for(int iteration=0;iteration<25;iteration++){
   auto begin=Clock::now();result=session.Run(run,inputs,&tensor,1,outputs,1);
   double ms=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
   if(iteration>=5)samples.push_back(ms);
  }
  size_t count=result[0].GetTensorTypeAndShapeInfo().GetElementCount();std::vector<float> converted;
  if(half){auto* data=result[0].GetTensorData<Ort::Float16_t>();converted.reserve(count);for(size_t i=0;i<count;i++)converted.push_back(data[i].ToFloat());}
  const auto* pixels=half?converted.data():result[0].GetTensorData<float>();
  float minimum=1,maximum=0;for(size_t i=0;i<count;i++){if(!std::isfinite(pixels[i]))throw std::runtime_error("Non-finite neural output");minimum=std::min(minimum,pixels[i]);maximum=std::max(maximum,pixels[i]);}
  if(minimum<-.001f||maximum>1.001f)throw std::runtime_error("Output outside bounded model range");
  if(argc>4){std::ofstream file(argv[4],std::ios::binary);file.write((const char*)pixels,count*sizeof(float));if(!file)throw std::runtime_error("Cannot write output fixture");}
  std::sort(samples.begin(),samples.end());
  printf("NEURAL_BENCH ort=%s backend=%s dtype=%s cpu_fallback=%d input=%lldx%lld output=%lldx%lld warmup=5 samples=%zu session_ms=%.3f median_ms=%.3f p95_ms=%.3f min_ms=%.3f max_ms=%.3f output_min=%.6f output_max=%.6f\n",
   OrtGetApiBase()->GetVersionString(),backend.c_str(),half?"fp16":"fp32",backend=="cpu",(long long)shape[3],(long long)shape[2],(long long)outputShape[3],(long long)outputShape[2],samples.size(),sessionMs,samples[samples.size()/2],samples[size_t(samples.size()*.95)],samples.front(),samples.back(),minimum,maximum);
  if(argc>5){Ort::AllocatorWithDefaultOptions allocator;auto path=session.EndProfilingAllocated(allocator);printf("ORT_PROFILE %s\n",path.get());}
  return 0;
 }catch(const std::exception& error){fprintf(stderr,"NEURAL_ERROR %s\n",error.what());return 1;}
}
