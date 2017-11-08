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
                divs += string.Format("<h2>{0} packets per second for {1} seconds, {2} bytes per packet ({3} bytes per sec), {4} packets lost({5}%)",
                    packets_per_s, duration_s, packet_size, duration_s * packet_size, num_dropped_packets, packet_loss_perc);
                if(num_duplicate_packets > 0)
                {
                    divs += ", " + num_duplicate_packets.ToString() + " packets duplicated(" + packet_dupe_perc.ToString() + "%)";
                }
                divs += "</h2>";
                
                // jitter: compute average delta, because first packet could have jitter that throws the whole graph off
                double inv_num_packets = 1.0 / num_packets;
                double avg_jitter_s = 0.0;
                for (int i = 0; i < num_packets; ++i)
                {
                    if (packets_delivered[i])
                    {
                        double expected_timestamp_s = i * packet_interval_s;
                        double delta_s = packet_timestamps[i] - expected_timestamp_s;
                        avg_jitter_s += delta_s * inv_num_packets;
                    }
                }

                // jitter chart
                Chart chart = new Chart();
                chart.title = "Jitter and Packet Loss";
                chart.x_axis = "Packet Number";
                chart.name = "jitter" + test_i.ToString();
                chart.title = "Jitter";
                List<string> y_axes = new List<string>(new string[] { "Jitter (ms)" });
                List<Chart.Series> series = new List<Chart.Series>(new Chart.Series[] { Chart.CreateSeries("Min", "area", 0), Chart.CreateSeries("Max", "area", 0), Chart.CreateSeries("Avg", "area", 0) });
                if(num_dropped_packets > 0)
                {
                    y_axes.Add("Num Packets");
                    series.Add(Chart.CreateSeries("Dropped Packets", "bars", 1));
                }
                chart.y_axes = y_axes.ToArray();
                chart.series = series.ToArray();
                BeginChart(ref chart, writer);

                const uint c_max_points_per_graph = 16000;
                uint num_packets_in_slice = 0;
                double min_jitter_ms = 0.0;
                double max_jitter_ms = 0.0;
                double total_jitter_ms = 0.0;
                uint num_dropped_packets_in_slice = 0;
                uint stride = Math.Max(1, num_packets / 16000);
                for (int i = 0; i < num_packets; ++i)
                {
                    if (packets_delivered[i])
                    {
                        double expected_timestamp_s = i * packet_interval_s;
                        double jitter_ms = Math.Abs((packet_timestamps[i] - expected_timestamp_s - avg_jitter_s) * 1000.0);

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
                    else
                    {
                        ++num_dropped_packets_in_slice;
                    }

                    if ((i + 1) % stride == 0 || i == (num_packets - 1))
                    {
                        double avg_jitter_ms = total_jitter_ms;
                        if (num_packets_in_slice > 0)
                        {
                            total_jitter_ms /= num_packets_in_slice;
                        }

                        writer.Write(string.Format(",[{0}, {1}, {2}, {3}", i - stride + 1, min_jitter_ms, max_jitter_ms, avg_jitter_ms));
                        if (num_dropped_packets > 0)
                        {
                            writer.Write(", " + num_dropped_packets_in_slice.ToString());
                        }
                        writer.Write("]");

                        num_packets_in_slice = 0;
                        num_dropped_packets_in_slice = 0;
                        total_jitter_ms = 0.0;
                    }
                }
                uint chart_width = num_packets / stride;
                EndChart(ref chart, writer, ref divs, chart_width);
            }

            writer.WriteLine("}</script></head><body>" + divs + "</body></html>");
            writer.Flush();
            out_file_stream.Close();
        }

        struct Chart
        {
            public string name;
            public string x_axis;
            public Series[] series;
            public string[] y_axes;
            public string title;

            public struct Series
            {
                public string name;
                public string type;
                public int y_axis;
            }

            public static Series CreateSeries(string name, string type, int y_axis)
            {
                Series s;
                s.name = name;
                s.type = type;
                s.y_axis = y_axis;
                return s;
            }
        }

        static void BeginChart(ref Chart chart, StreamWriter writer)
        {
            writer.Write("var " + chart.name + "_data = google.visualization.arrayToDataTable([['" + chart.x_axis + "'");
            foreach(Chart.Series series in chart.series)
            {
                writer.Write(", '" + series.name + "'");
            }
            writer.Write("]");
        }

        static void EndChart(ref Chart chart, StreamWriter writer, ref string divs, uint width)
        {
            writer.WriteLine("]);");

            writer.WriteLine("var " + chart.name + "_options = {title: '" + chart.title + "', width: '" + width.ToString() + "px', height: '800px', pointSize: 1,");

            string seriesString = "";
            for(int i = 0; i < chart.series.Length; ++i)
            {
                Chart.Series series = chart.series[i];

                if(i > 0)
                {
                    seriesString += ",";
                }

                seriesString += i.ToString() + ": {targetAxisIndex: '" + series.y_axis.ToString() + "', type: '" + series.type + "'}";
            }

            string axesString = "";
            for (int i = 0; i < chart.y_axes.Length; ++i)
            {
                string y_axis = chart.y_axes[i];

                if(i > 0)
                {
                    axesString += ",";
                }

                axesString += i.ToString() + ": {title: '" + y_axis + "'}";
            }

            writer.WriteLine("series: {" + seriesString + "}, vAxes: {" + axesString + "}");
            writer.WriteLine("};");

            writer.WriteLine(string.Format("var {0}_chart = new google.visualization.ComboChart(document.getElementById('{0}_div'));", chart.name));
            writer.WriteLine(string.Format("{0}_chart.draw({0}_data, {0}_options); ", chart.name));

            divs += string.Format("<div id = \"{0}_div\" style=\"width: {1}px; height: 800px; \"></div>", chart.name, width);
        }
    }
}
