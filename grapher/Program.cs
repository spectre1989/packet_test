﻿using System.IO;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;

namespace grapher
{
    class Program
    {
        static void Main(string[] args)
        {
            foreach(string arg in args)
            {
                if(File.Exists(arg))
                {
                    CreateReport(arg);
                }
                else if(Directory.Exists(arg))
                {
                    ProcessDir(arg);
                }
            }

            Console.Write("done");
        }

        static void ProcessDir(string dir)
        {
            foreach (string file in Directory.GetFiles(dir))
            {
                CreateReport(file);
            }
            foreach (string subdir in Directory.GetDirectories(dir))
            {
                ProcessDir(subdir);
            }
        }

        static void CreateReport(string path)
        {
            string extension = Path.GetExtension(path);
            if(extension != ".txt" && extension != ".json")
            {
                Console.WriteLine("ignoring " + path);
                return;
            }

            string out_file_path = Path.ChangeExtension(path, "html");
            if(File.Exists(out_file_path))
            {
                File.Delete(out_file_path);
            }
            FileStream out_file_stream = File.OpenWrite(out_file_path);
            StreamWriter writer = new StreamWriter(out_file_stream);
            writer.Write("<html><head><script type = \"text/javascript\" src = \"https://www.gstatic.com/charts/loader.js\"></script><script type = \"text/javascript\">google.charts.load('current', { 'packages':['corechart'] });google.charts.setOnLoadCallback(drawChart); function drawChart(){");

            string divs = "<h1>" + Path.GetFileNameWithoutExtension(path) + "</h1>";

            JObject root = JObject.Parse(new StreamReader(File.OpenRead(path)).ReadToEnd());
            JArray tests = root["tests"] as JArray;
            for (int test_i = 0; test_i < tests.Count; ++test_i)
            {
                JObject test = tests[test_i] as JObject;
                UInt32 duration_s = test["duration_s"].ToObject<UInt32>();
                UInt32 packets_per_s = test["packets_per_s"].ToObject<UInt32>();
                UInt32 packet_size = test["packet_size"].ToObject<UInt32>();
                UInt32 num_packets = duration_s * packets_per_s;
                double packet_interval_s = 1.0 / packets_per_s;


                bool[] packets_delivered = new bool[num_packets];
                double[] packet_timestamps = new double[num_packets];
                for (int i = 0; i < num_packets; ++i)
                {
                    packets_delivered[i] = false;
                }
                UInt32 num_dropped_packets = 0;
                UInt32 num_duplicate_packets = 0;

                JArray packets = test["packets"] as JArray;
                foreach (JObject packet in packets)
                {
                    UInt32 id = packet["id"].ToObject<UInt32>();
                    double t = packet["t"].ToObject<double>();

                    if (!packets_delivered[id])
                    {
                        packets_delivered[id] = true;
                        packet_timestamps[id] = t;
                    }
                    else
                    {
                        ++num_duplicate_packets;
                    }
                }

                for (int i = 0; i < num_packets; ++i)
                {
                    if (!packets_delivered[i])
                    {
                        ++num_dropped_packets;
                    }
                }

                float packet_loss_perc = (num_dropped_packets * 100) / (float)num_packets;
                float packet_dupe_perc = (num_duplicate_packets * 100) / (float)num_packets;


                // Title
                divs += string.Format("<h2>{0} packets per second for {1} seconds, {2} bytes per packet ({3} bytes per sec), {4} packets lost({5}%), {6} packets duplicated({7}%)</h2>",
                    packets_per_s, duration_s, packet_size, duration_s * packet_size, num_dropped_packets, packet_loss_perc, num_duplicate_packets, packet_dupe_perc);

                // delivered packets
                Chart chart = new Chart();
                chart.name = string.Format("deliveredPackets{0}", test_i);
                chart.x_axis = "Packet Number";
                chart.y_axis_title = "Time (sec)";
                chart.y_axes = new string[] { "Time (sec)" };
                chart.title = "Delivered Packets";
                chart.type = "ScatterChart";
                BeginChart(ref chart, writer);

                const uint c_max_points_per_graph = 1000;
                uint num_delivered_packets = num_packets - num_dropped_packets;
                uint stride = Math.Max(1, num_delivered_packets / c_max_points_per_graph);
                uint stride_counter = 0;
                for (int i = 0; i < num_packets; ++i)
                {
                    if (packets_delivered[i])
                    {
                        if(stride_counter == 0)
                        {
                            writer.Write(string.Format(",[{0}, {1}]", i, packet_timestamps[i]));
                        }
                        stride_counter = (stride_counter + 1) % stride;
                    }
                }
                EndChart(ref chart, writer, ref divs);

                // dropped packets chart
                if (num_dropped_packets > 0)
                {
                    chart.name = string.Format("droppedPackets{0}", test_i);
                    chart.title = "Dropped Packets";
                    BeginChart(ref chart, writer);

                    stride = Math.Max(1, num_dropped_packets / c_max_points_per_graph);
                    stride_counter = 0;
                    for (int i = 0; i < num_packets; ++i)
                    {
                        if (!packets_delivered[i])
                        {
                            if (stride_counter == 0)
                            {
                                writer.Write(string.Format(",[{0}, {1}]", i, i * packet_interval_s));
                            }
                            stride_counter = (stride_counter + 1) % stride;
                        }
                    }
                    EndChart(ref chart, writer, ref divs);
                }

                // jitter chart
                chart.name = string.Format("jitter{0}", test_i);
                chart.title = "Jitter";
                chart.y_axis_title = "Time (ms)";
                chart.y_axes = new string[] { "Time (ms)" };
                BeginChart(ref chart, writer);

                // compute average delta, because first packet could have jitter that throws the whole graph off
                double inv_num_packets = 1.0 / num_packets;
                double avg_delta_s = 0.0;
                stride = Math.Max(1, num_delivered_packets / c_max_points_per_graph);
                stride_counter = 0;
                for (int i = 0; i < num_packets; ++i)
                {
                    if (packets_delivered[i])
                    {
                        double expected_timestamp_s = i * packet_interval_s;
                        double delta_s = packet_timestamps[i] - expected_timestamp_s;
                        avg_delta_s += delta_s * inv_num_packets;

                        if (stride_counter == 0)
                        {
                            writer.Write(string.Format(",[{0}, {1}]", i, Math.Abs(delta_s) * 1000.0));
                        }
                        stride_counter = (stride_counter + 1) % stride;
                    }
                }
                EndChart(ref chart, writer, ref divs);

                // adjusted jitter chart
                chart.name = string.Format("adustedJitter{0}", test_i);
                chart.title = "Adjusted Jitter";
                BeginChart(ref chart, writer);

                stride_counter = 0;
                for (int i = 0; i < num_packets; ++i)
                {
                    if (packets_delivered[i])
                    {
                        if (stride_counter == 0)
                        {
                            double expected_timestamp_s = i * packet_interval_s;
                            double adjusted_delta_s = packet_timestamps[i] - expected_timestamp_s - avg_delta_s;
                            writer.Write(string.Format(",[{0}, {1}]", i, Math.Abs(adjusted_delta_s) * 1000.0));
                        }
                        stride_counter = (stride_counter + 1) % stride;
                    }
                }
                EndChart(ref chart, writer, ref divs);

                // jitter chart
                chart.name = "jitterArea" + test_i.ToString();
                chart.title = "Jitter";
                chart.y_axes = new string[] { "Min", "Max", "Avg" };
                chart.type = "AreaChart";
                BeginChart(ref chart, writer);

                uint num_packets_in_slice = 0;
                double min_jitter_ms = 0.0;
                double max_jitter_ms = 0.0;
                double total_jitter_ms = 0.0;
                stride = Math.Max(1, num_packets / c_max_points_per_graph);
                for (int i = 0; i < num_packets; ++i)
                {
                    if (packets_delivered[i])
                    {
                        double expected_timestamp_s = i * packet_interval_s;
                        double jitter_ms = Math.Abs((packet_timestamps[i] - expected_timestamp_s - avg_delta_s) * 1000.0);

                        ++num_packets_in_slice;
                        total_jitter_ms += jitter_ms;
                        if(num_packets_in_slice == 1)
                        {
                            max_jitter_ms = jitter_ms;
                            min_jitter_ms = jitter_ms;
                        }
                        else
                        {
                            max_jitter_ms = Math.Max(max_jitter_ms, jitter_ms);
                            min_jitter_ms = Math.Min(min_jitter_ms, jitter_ms);
                        }
                    }

                    if ((i + 1) % stride == 0 && num_packets_in_slice > 0)
                    {
                        double avg_jitter_ms = total_jitter_ms / num_packets_in_slice;

                        writer.Write(string.Format(",[{0}, {1}, {2}, {3}]", i - stride + 1, min_jitter_ms, max_jitter_ms, avg_jitter_ms));

                        num_packets_in_slice = 0;
                        total_jitter_ms = 0.0;
                    }
                }
                EndChart(ref chart, writer, ref divs);
            }

            writer.Write("}</script></head><body>" + divs + "</body></html>");
            writer.Flush();
            out_file_stream.Close();
        }

        struct Chart
        {
            public string name;
            public string x_axis;
            public string[] y_axes;
            public string y_axis_title;
            public string title;
            public string type;
        }

        static void BeginChart(ref Chart chart, StreamWriter writer)
        {
            writer.Write("var " + chart.name + "_data = google.visualization.arrayToDataTable([['" + chart.x_axis + "'");
            foreach(string y_axis in chart.y_axes)
            {
                writer.Write(", '" + y_axis + "'");
            }
            writer.Write("]");
        }

        static void EndChart(ref Chart chart, StreamWriter writer, ref string divs)
        {
            writer.Write("]);");
            writer.Write(string.Format("var {0}_options = {{title: '{1}',hAxis: {{title: '{2}'}}, vAxis: {{title: '{3}'}}, legend: 'none', pointSize: 1}};", chart.name, chart.title, chart.x_axis, chart.y_axis_title));
            writer.Write(string.Format("var {0}_chart = new google.visualization.{1}(document.getElementById('{0}_div'));", chart.name, chart.type));
            writer.Write(string.Format("{0}_chart.draw({0}_data, {0}_options); ", chart.name));

            divs += string.Format("<div id = \"{0}_div\" style=\"width: 100%; height: 800px; \"></div>", chart.name);
        }
    }
}
