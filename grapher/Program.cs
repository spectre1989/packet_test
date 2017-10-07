using System.IO;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;

namespace grapher
{
    class Program
    {
        static void Main(string[] args)
        {
            FileStream file_stream = File.OpenWrite("report.html");
            StreamWriter writer = new StreamWriter(file_stream);
            writer.WriteLine("<html><head><script type = \"text/javascript\" src = \"https://www.gstatic.com/charts/loader.js\"></script><script type = \"text/javascript\">google.charts.load('current', { 'packages':['corechart'] });google.charts.setOnLoadCallback(drawChart); function drawChart(){");

            string divs = "";

            JObject root = JObject.Parse(new StreamReader(File.OpenRead(args[0])).ReadToEnd());
            JArray tests = root["tests"] as JArray;
            for (int test_i = 0; test_i < tests.Count; ++test_i)
            {
                JObject test = tests[test_i] as JObject;
                UInt32 duration_s = test["duration_s"].ToObject<UInt32>();
                UInt32 packets_per_s = test["packets_per_s"].ToObject<UInt32>();
                UInt32 packet_size = test["packet_size"].ToObject<UInt32>();
                UInt32 num_packets = duration_s * packets_per_s;
                float packet_interval_s = 1.0f / (float)packets_per_s;

                bool[] packets_delivered = new bool[num_packets];
                for(int i = 0; i < num_packets; ++i)
                {
                    packets_delivered[i] = false;
                }

                writer.WriteLine(string.Format("var data{0} = google.visualization.arrayToDataTable([['Packet Number', {{label: 'Delivered', type: 'number'}}, {{label: 'Dropped', type: 'number'}}]", test_i));

                // delivered packets
                JArray packets = test["packets"] as JArray;
                if (packets.Count > 0)
                {
                    float t_offset = packets[0]["id"].ToObject<UInt32>() * packet_interval_s;

                    foreach (JObject packet in packets)
                    {
                        UInt32 id = packet["id"].ToObject<UInt32>();
                        float t = packet["t"].ToObject<float>() + t_offset;
                        writer.WriteLine(string.Format(",[{0}, {1}, null]", id + 1, t));

                        packets_delivered[id] = true;
                    }
                }

                // dropped packets
                for(int i = 0; i < num_packets; ++i)
                {
                    if(!packets_delivered[i])
                    {
                        writer.WriteLine(string.Format(",[{0}, null, {1}]", i + 1, i * packet_interval_s));
                    }
                }
                
                writer.WriteLine(string.Format("]);var options{4} = {{title: '{0} packets per second for {1} seconds, {2} bytes per packet',hAxis: {{ title: 'Packet Number', minValue: 1, maxValue: {3}}}, vAxis: {{ title: 'Time (seconds)', minValue: 0}}, legend: 'none', pointSize: 1}};var chart{4} = new google.visualization.ScatterChart(document.getElementById('chart{4}_div')); chart{4}.draw(data{4}, options{4}); ", packets_per_s, duration_s, packet_size, num_packets, test_i));

                divs += string.Format("<div id = \"chart{0}_div\" style=\"width: 100%; height: 800px; \"></div>", test_i);
            }

            writer.WriteLine(string.Format("}}</script></head><body>{0}</body></html>", divs));
            writer.Flush();
            file_stream.Close();

            Console.WriteLine("done");
        }
    }
}
